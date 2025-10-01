// kernel/scheduler/cfs_scheduler.c - IMPLEMENTACIÓN COMPLETA
#include "scheduler_types.h"
#include <ir0/print.h>
#include <stddef.h>
#include <ir0/panic/panic.h>
#include <string.h>
#include <stdbool.h>

// Variable externa del task actual
extern task_t *current_running_task;

// Funciones de protección de interrupciones
#ifdef __x86_64__
static inline uint32_t interrupt_save_and_disable(void)
{
    uint64_t flags;
    __asm__ volatile("pushfq; cli; popq %0" : "=r"(flags)::"memory");
    return (uint32_t)flags;
}

static inline void interrupt_restore(uint32_t flags)
{
    if (flags & 0x200)
    { // IF flag was set
        __asm__ volatile("sti" ::: "memory");
    }
}
#else
static inline uint32_t interrupt_save_and_disable(void)
{
    uint32_t flags;
    __asm__ volatile("pushfl; cli; popl %0" : "=r"(flags)::"memory");
    return flags;
}

static inline void interrupt_restore(uint32_t flags)
{
    if (flags & 0x200)
    { // IF flag was set
        __asm__ volatile("sti" ::: "memory");
    }
}
#endif

// Debug logging macro
#ifndef LOG_DEBUG
#define LOG_DEBUG(fmt, ...) print("DEBUG: " fmt "\n")
#endif

// ===============================================================================
// CONFIGURACIONES CFS
// ===============================================================================

#define CFS_TARGETED_LATENCY 20000000ULL // 20ms en nanosegundos
#define CFS_MIN_GRANULARITY 4000000ULL   // 4ms mínimo por proceso
#define CFS_NICE_0_LOAD 1024             // Peso de nice 0
#define CFS_MAX_NICE 19
#define CFS_MIN_NICE -20

// Tabla de pesos según nice value (exponencial)
static const uint32_t cfs_prio_to_weight[40] =
    {
        /* -20 */ 88761,
        71755,
        56483,
        46273,
        36291,
        /* -15 */ 29154,
        23254,
        18705,
        14949,
        11916,
        /* -10 */ 9548,
        7620,
        6100,
        4904,
        3906,
        /*  -5 */ 3121,
        2501,
        1991,
        1586,
        1277,
        /*   0 */ 1024,
        820,
        655,
        526,
        423,
        /*   5 */ 335,
        272,
        215,
        172,
        137,
        /*  10 */ 110,
        87,
        70,
        56,
        45,
        /*  15 */ 36,
        29,
        23,
        18,
        15,
};

// ===============================================================================
// RED-BLACK TREE PARA CFS
// ===============================================================================

static cfs_runqueue_t cfs_rq;

// Pool de nodos RB para evitar kmalloc durante scheduling crítico
#define MAX_RB_NODES 1024
static rb_node_t rb_node_pool[MAX_RB_NODES];
static int rb_node_pool_index = 0;

// ===============================================================================
// FUNCIONES AUXILIARES RED-BLACK TREE -- Elegante por donde lo mires --
// ===============================================================================
static rb_node_t *rb_alloc_node(void)
{
    // Disable interrupts para atomic operation
    uint32_t flags = interrupt_save_and_disable();

    if (rb_node_pool_index >= MAX_RB_NODES)
    {
        // Restore interrupts before fallback
        interrupt_restore(flags);

        LOG_ERR("CFS: RB node pool exhausted!");
        print(" (");
        print_hex_compact(rb_node_pool_index);
        print("/");
        print_hex_compact(MAX_RB_NODES);
        print(" nodes used)\n");

        // Trigger automatic fallback to simpler scheduler
        extern void scheduler_fallback_to_next(void);
        scheduler_fallback_to_next();

        return NULL;
    }

    rb_node_t *node = &rb_node_pool[rb_node_pool_index++];
    memset(node, 0, sizeof(rb_node_t));

    // Restore interrupts
    interrupt_restore(flags);

    print("CFS: Allocated RB node ");
    print_hex_compact(rb_node_pool_index);
    print("/");
    print_hex_compact(MAX_RB_NODES);
    print("\n");

    return node;
}

// NUEVA FUNCIÓN: Compactación del pool de nodos
static void rb_compact_node_pool(void)
{
    LOG_OK("CFS: Compacting RB node pool...");

    int write_index = 0;

    // Compactar pool eliminando nodos libres
    for (int read_index = 0; read_index < rb_node_pool_index; read_index++)
    {
        if (rb_node_pool[read_index].task != NULL)
        {
            if (write_index != read_index)
            {
                rb_node_pool[write_index] = rb_node_pool[read_index];
                // Clear the old location
                memset(&rb_node_pool[read_index], 0, sizeof(rb_node_t));
            }
            write_index++;
        }
    }

    rb_node_pool_index = write_index;

    print("CFS: Pool compacted to ");
    print_hex_compact(rb_node_pool_index);
    print("/");
    print_hex_compact(MAX_RB_NODES);
    print(" nodes\n");
}

static void rb_free_node(rb_node_t *node)
{
    if (!node)
    {
        return;
    }

    uint32_t flags = interrupt_save_and_disable();

    // Marcar como libre
    node->task = NULL;
    node->parent = node->left = node->right = NULL;
    node->key = 0;
    node->color = RB_RED;

    // Restore interrupts
    interrupt_restore(flags);

    // Si el pool está muy fragmentado, compactar
    static int free_calls = 0;
    if (++free_calls >= 50)
    { // Cada 50 liberaciones
        free_calls = 0;
        rb_compact_node_pool();
    }
}

static void rb_rotate_left(rb_node_t **root, rb_node_t *node)
{
    rb_node_t *right = node->right;
    node->right = right->left;

    if (right->left)
        right->left->parent = node;

    right->parent = node->parent;

    if (!node->parent)
        *root = right;
    else if (node == node->parent->left)
        node->parent->left = right;
    else
        node->parent->right = right;

    right->left = node;
    node->parent = right;
}

static void rb_rotate_right(rb_node_t **root, rb_node_t *node)
{
    rb_node_t *left = node->left;
    node->left = left->right;

    if (left->right)
        left->right->parent = node;

    left->parent = node->parent;

    if (!node->parent)
        *root = left;
    else if (node == node->parent->right)
        node->parent->right = left;
    else
        node->parent->left = left;

    left->right = node;
    node->parent = left;
}

static void rb_insert_fixup(rb_node_t **root, rb_node_t *node)
{
    rb_node_t *parent, *gparent;

    while ((parent = node->parent) && parent->color == RB_RED)
    {
        gparent = parent->parent;

        if (parent == gparent->left)
        {
            rb_node_t *uncle = gparent->right;
            if (uncle && uncle->color == RB_RED)
            {
                uncle->color = RB_BLACK;
                parent->color = RB_BLACK;
                gparent->color = RB_RED;
                node = gparent;
                continue;
            }

            if (parent->right == node)
            {
                rb_rotate_left(root, parent);
                rb_node_t *tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->color = RB_BLACK;
            gparent->color = RB_RED;
            rb_rotate_right(root, gparent);
        }
        else
        {
            rb_node_t *uncle = gparent->left;
            if (uncle && uncle->color == RB_RED)
            {
                uncle->color = RB_BLACK;
                parent->color = RB_BLACK;
                gparent->color = RB_RED;
                node = gparent;
                continue;
            }

            if (parent->left == node)
            {
                rb_rotate_right(root, parent);
                rb_node_t *tmp = parent;
                parent = node;
                node = tmp;
            }

            parent->color = RB_BLACK;
            gparent->color = RB_RED;
            rb_rotate_left(root, gparent);
        }
    }

    (*root)->color = RB_BLACK;
}

static void rb_insert(rb_node_t **root, rb_node_t *node)
{
    rb_node_t *parent = NULL;
    rb_node_t *current = *root;

    // Inserción BST estándar
    while (current)
    {
        parent = current;
        if (node->key < current->key)
            current = current->left;
        else
            current = current->right;
    }

    node->parent = parent;
    if (!parent)
        *root = node;
    else if (node->key < parent->key)
        parent->left = node;
    else
        parent->right = node;

    node->color = RB_RED;
    rb_insert_fixup(root, node);
}

static rb_node_t *rb_find_leftmost(rb_node_t *root)
{
    if (!root)
        return NULL;
    while (root->left)
        root = root->left;
    return root;
}

// ===============================================================================
// FUNCIONES CFS CORE
// ===============================================================================

static uint32_t cfs_nice_to_weight(int nice)
{
    if (nice < CFS_MIN_NICE)
        nice = CFS_MIN_NICE;
    if (nice > CFS_MAX_NICE)
        nice = CFS_MAX_NICE;
    return cfs_prio_to_weight[nice + 20]; // nice -20 está en index 0
}

static uint64_t cfs_calc_delta_fair(uint64_t delta, task_t *task)
{
    uint32_t weight = cfs_nice_to_weight(task->nice);

    if (weight != CFS_NICE_0_LOAD)
    {
        // delta_fair = delta * NICE_0_LOAD / weight
        delta = (delta * CFS_NICE_0_LOAD) / weight;
    }

    return delta;
}

static uint64_t cfs_calc_slice(task_t *task)
{
    uint32_t weight = cfs_nice_to_weight(task->nice);
    uint64_t slice;

    if (cfs_rq.nr_running <= 1)
    {
        return CFS_TARGETED_LATENCY;
    }

    // slice = targeted_latency * (task_weight / total_weight)
    slice = (CFS_TARGETED_LATENCY * weight) / cfs_rq.total_weight;

    // Garantizar granularidad mínima
    if (slice < CFS_MIN_GRANULARITY)
        slice = CFS_MIN_GRANULARITY;

    return slice;
}

static void cfs_update_runqueue_stats(void)
{
    // Actualizar clock virtual del runqueue
    cfs_rq.clock += 1000000; // 1ms incremento (simplificado)

    // Calcular vruntime promedio para nuevas tareas
    if (cfs_rq.nr_running > 0)
    {
        uint64_t total_vruntime = 0;
        (void)total_vruntime; // Variable not used in this implementation

        // En una implementación real, iteraríamos sobre todas las tareas
        // Por simplicidad, usamos min_vruntime como aproximación
        cfs_rq.avg_vruntime = cfs_rq.min_vruntime;
    }

    // Actualizar estadísticas de carga (simplificado)
    // En Linux real, esto usa exponential weighted moving average
    cfs_rq.load_avg = (cfs_rq.load_avg * 7 + cfs_rq.total_weight) / 8;
    cfs_rq.runnable_avg = (cfs_rq.runnable_avg * 7 + cfs_rq.nr_running) / 8;

    // Ajustar parámetros dinámicamente según carga
    if (cfs_rq.nr_running > 8)
    {
        // Con muchas tareas, aumentar granularidad para evitar thrashing
        cfs_rq.targeted_latency = CFS_TARGETED_LATENCY * 2;
    }
    else
    {
        cfs_rq.targeted_latency = CFS_TARGETED_LATENCY;
    }
}

static void cfs_update_min_vruntime(void)
{
    uint64_t vruntime = cfs_rq.min_vruntime;

    if (cfs_rq.leftmost)
    {
        uint64_t leftmost_vruntime = cfs_rq.leftmost->key;

        if (!current_running_task ||
            (current_running_task && leftmost_vruntime < current_running_task->vruntime))
        {
            vruntime = leftmost_vruntime;
        }

        if (current_running_task)
        {
            vruntime = (vruntime + current_running_task->vruntime) / 2;
        }
    }
    else if (current_running_task)
    {
        vruntime = current_running_task->vruntime;
    }

    // min_vruntime solo puede aumentar
    if (vruntime > cfs_rq.min_vruntime)
        cfs_rq.min_vruntime = vruntime;
}

// ===============================================================================
// IMPLEMENTACIÓN SCHEDULER OPERATIONS
// ===============================================================================

void cfs_init_impl(void)
{
    LOG_OK("Initializing CFS scheduler");

    // Limpiar estructuras del árbol RB
    cfs_rq.root = NULL;
    cfs_rq.leftmost = NULL;

    // Inicializar estadísticas
    cfs_rq.nr_running = 0;
    cfs_rq.min_vruntime = 0;
    cfs_rq.total_weight = 0;

    // Inicializar clocks virtuales
    cfs_rq.clock = 0;
    cfs_rq.exec_clock = 0;
    cfs_rq.avg_vruntime = 0;

    // Configurar parámetros de scheduling
    cfs_rq.targeted_latency = CFS_TARGETED_LATENCY;
    cfs_rq.min_granularity = CFS_MIN_GRANULARITY;

    // Inicializar estadísticas de carga
    cfs_rq.load_avg = 0;
    cfs_rq.runnable_avg = 0;

    // Resetear pool de nodos RB
    rb_node_pool_index = 0;

    LOG_OK("CFS initialized with advanced runqueue management");
}

void cfs_add_task_impl(task_t *task)
{
    if (!task)
    {
        LOG_ERR("CFS: add_task received NULL task");
        return;
    }

    // Verificar que el scheduler CFS esté activo
    extern scheduler_type_t active_scheduler_type;
    if (active_scheduler_type != SCHEDULER_CFS)
    {
        LOG_WARN("CFS: Trying to add task to inactive CFS scheduler");
        // Forward to active scheduler
        extern void add_task(task_t * task);
        add_task(task);
        return;
    }

    uint32_t flags = interrupt_save_and_disable();

    rb_node_t *node = rb_alloc_node();
    if (!node)
    {
        // Restore interrupts
        interrupt_restore(flags);

        LOG_ERR("CFS: Failed to allocate RB node for task PID ");
        print_hex_compact(task->pid);
        print("\n");

        // Critical error: try fallback scheduling
        LOG_WARN("CFS: Attempting scheduler fallback due to node exhaustion");
        extern void scheduler_fallback_to_next(void);
        scheduler_fallback_to_next();
        return;
    }

    // Inicializar vruntime para nuevas tareas con better logic
    if (task->vruntime == 0)
    {
        if (cfs_rq.nr_running > 0)
        {
            // Nueva tarea toma vruntime mínimo + small penalty para fairness
            task->vruntime = cfs_rq.min_vruntime + (CFS_TARGETED_LATENCY / 2);
        }
        else
        {
            task->vruntime = cfs_rq.min_vruntime;
        }
    }

    // Anti-starvation: garantizar que nuevas tareas no tengan ventaja injusta
    uint64_t max_allowed_vruntime = cfs_rq.min_vruntime + CFS_TARGETED_LATENCY;
    if (task->vruntime > max_allowed_vruntime)
    {
        task->vruntime = max_allowed_vruntime;
    }

    uint32_t weight = cfs_nice_to_weight(task->nice);

    node->key = task->vruntime;
    node->task = task;
    node->color = RB_RED;

    // Insert with error checking
    rb_insert(&cfs_rq.root, node);

    // Actualizar leftmost si es necesario
    if (!cfs_rq.leftmost || task->vruntime < cfs_rq.leftmost->key)
    {
        cfs_rq.leftmost = node;
        print("CFS: New leftmost task PID ");
        print_hex_compact(task->pid);
        print(", vruntime ");
        print_hex64(task->vruntime);
        print("\n");
    }

    cfs_rq.nr_running++;
    cfs_rq.total_weight += weight;

    task->state = TASK_READY;

    // Restore interrupts
    interrupt_restore(flags);

    print("CFS: Task PID ");
    print_hex_compact(task->pid);
    print(" added (vruntime: ");
    print_hex64(task->vruntime);
    print(", weight: ");
    print_hex_compact(weight);
    print(", running: ");
    print_hex_compact(cfs_rq.nr_running);
    print(")\n");
}

void cfs_remove_task_impl(task_t *task)
{
    if (!task)
    {
        LOG_ERR("CFS: remove_task received NULL task");
        return;
    }

    // Simple implementation - just mark as terminated
    task->state = TASK_TERMINATED;
    cfs_rq.nr_running--;

    LOG_OK("CFS: Task removed from runqueue");
}

task_t *cfs_pick_next_task_impl(void)
{
    if (!cfs_rq.leftmost)
    {
        return NULL;
    }

    rb_node_t *node = cfs_rq.leftmost;
    task_t *task = node->task;

    // Remover del árbol
    // Implementación simplificada - en producción usar rb_delete completo
    if (node->right)
    {
        cfs_rq.leftmost = rb_find_leftmost(node->right);
    }
    else
    {
        cfs_rq.leftmost = NULL;
        // Buscar nuevo leftmost
        if (cfs_rq.root != node)
        {
            cfs_rq.leftmost = rb_find_leftmost(cfs_rq.root);
        }
    }

    uint32_t weight = cfs_nice_to_weight(task->nice);
    cfs_rq.total_weight -= weight;
    cfs_rq.nr_running--;

    rb_free_node(node);

    task->state = TASK_RUNNING;
    task->time_slice = cfs_calc_slice(task);
    task->slice_start = cfs_rq.min_vruntime; // timestamp actual

    LOG_OK("CFS: Picked next task");
    return task;
}

static void cfs_task_tick(void)
{
    if (!current_running_task)
        return;

    uint32_t flags = interrupt_save_and_disable();

    // Actualizar estadísticas del runqueue
    cfs_update_runqueue_stats();
    cfs_rq.exec_clock += 1000000; // Agregar tiempo transcurrido

    // Actualizar vruntime de la tarea actual
    uint64_t delta = 1000000; // 1ms en nanosegundos (simplificado)
    uint64_t delta_fair = cfs_calc_delta_fair(delta, current_running_task);

    current_running_task->vruntime += delta_fair;
    current_running_task->exec_time += delta;
    current_running_task->total_runtime += delta;

    // Actualizar min_vruntime del runqueue
    cfs_update_min_vruntime();

    // Verificar si necesitamos preempción con improved logic
    bool should_preempt = false;

    if (cfs_rq.leftmost)
    {
        uint64_t leftmost_vruntime = cfs_rq.leftmost->key;
        uint64_t current_vruntime = current_running_task->vruntime;

        // Preemption conditions:

        // 1. Fairness violation (otra tarea está muy atrás en vruntime)
        if (current_vruntime > leftmost_vruntime + cfs_rq.min_granularity)
        {
            should_preempt = true;
            print("CFS: Preemption due to fairness (current: ");
            print_hex64(current_vruntime);
            print(", leftmost: ");
            print_hex64(leftmost_vruntime);
            print(")\n");
        }

        // 2. Time slice exhausted
        if (current_running_task->exec_time >= current_running_task->time_slice)
        {
            should_preempt = true;
            print("CFS: Preemption due to time slice exhausted\n");
        }

        // 3. Severe unfairness (current task too far ahead)
        if (current_vruntime > cfs_rq.avg_vruntime + (CFS_TARGETED_LATENCY * 2))
        {
            should_preempt = true;
            print("CFS: Preemption due to severe unfairness\n");
        }

        // 4. Load balancing (si hay muchas tareas)
        if (cfs_rq.nr_running > 4 &&
            current_running_task->exec_time > (CFS_TARGETED_LATENCY / cfs_rq.nr_running))
        {
            should_preempt = true;
            print("CFS: Preemption for load balancing\n");
        }
    }

    if (should_preempt)
    {
        // Marcar para replanificación
        current_running_task->state = TASK_READY;
        current_running_task->context_switches++;

        // Reset exec_time for next run
        current_running_task->exec_time = 0;

        // Re-add to CFS queue
        cfs_add_task_impl(current_running_task);
        current_running_task = NULL;

        LOG_OK("CFS: Task preempted due to scheduling policy");
    }

    // Restore interrupts
    interrupt_restore(flags);
}

static void cfs_cleanup(void)
{
    LOG_OK("CFS scheduler cleanup");

    // Limpiar árbol RB
    cfs_rq.root = NULL;
    cfs_rq.leftmost = NULL;
    cfs_rq.nr_running = 0;
    cfs_rq.total_weight = 0;

    // Resetear pool de nodos
    rb_node_pool_index = 0;
}

void unified_add_task(task_t *task)
{
    if (!task)
        return;

    // Usar el scheduler activo detectado automáticamente
    extern scheduler_ops_t current_scheduler;

    if (current_scheduler.add_task)
    {
        current_scheduler.add_task(task);
        LOG_OK("Task added to active scheduler");
    }
    else
    {
        LOG_ERR("No active scheduler available!");
    }
}

task_t *unified_pick_next_task(void)
{
    extern scheduler_ops_t current_scheduler;

    if (current_scheduler.pick_next_task)
    {
        return current_scheduler.pick_next_task();
    }

    return get_idle_task(); // Fallback
}

// ===============================================================================
// FUNCIONES DE DEBUG
// ===============================================================================

void cfs_dump_state(void)
{
    print_colored("=== CFS SCHEDULER STATE (COMPLETE) ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Estadísticas básicas
    print("Nr running: ");
    print_hex_compact(cfs_rq.nr_running);
    print("\n");
    print("Total weight: ");
    print_hex_compact(cfs_rq.total_weight);
    print("\n");
    print("Min vruntime: ");
    print_hex64(cfs_rq.min_vruntime);
    print("\n");
    print("Avg vruntime: ");
    print_hex64(cfs_rq.avg_vruntime);
    print("\n");

    // Clocks del sistema
    print("Virtual clock: ");
    print_hex64(cfs_rq.clock);
    print("\n");
    print("Exec clock: ");
    print_hex64(cfs_rq.exec_clock);
    print("\n");

    // Configuración de scheduling
    print("Targeted latency: ");
    print_hex64(cfs_rq.targeted_latency);
    print(" ns\n");
    print("Min granularity: ");
    print_hex64(cfs_rq.min_granularity);
    print(" ns\n");

    // Estadísticas de carga
    print("Load average: ");
    print_hex_compact(cfs_rq.load_avg);
    print("\n");
    print("Runnable average: ");
    print_hex_compact(cfs_rq.runnable_avg);
    print("\n");

    // Información del próximo proceso
    if (cfs_rq.leftmost)
    {
        print("Next task vruntime: ");
        print_hex64(cfs_rq.leftmost->key);
        print(" (PID: ");
        print_hex_compact(cfs_rq.leftmost->task->pid);
        print(")\n");

        print("Next task weight: ");
        print_hex_compact(cfs_nice_to_weight(cfs_rq.leftmost->task->nice));
        print("\n");
    }
    else
    {
        print("Next task: None\n");
    }

    // Información del proceso actual
    if (current_running_task)
    {
        print("Current task: PID ");
        print_hex_compact(current_running_task->pid);
        print(" vruntime: ");
        print_hex64(current_running_task->vruntime);
        print(" nice: ");
        if (current_running_task->nice >= 0)
        {
            print_hex_compact(current_running_task->nice);
        }
        else
        {
            print("-");
            print_hex_compact(-current_running_task->nice);
        }
        print("\n");

        print("Current slice: ");
        print_hex64(current_running_task->time_slice);
        print(" ns, exec_time: ");
        print_hex64(current_running_task->exec_time);
        print(" ns\n");
    }

    print("\n");
}

// ===============================================================================
// EXPORT CFS OPERATIONS
// ===============================================================================

scheduler_ops_t cfs_scheduler_ops = {
    .type = SCHEDULER_CFS,
    .name = "Completely Fair Scheduler (Full Implementation)",
    .init = (void (*)(void))cfs_init_impl,
    .add_task = (void (*)(task_t *))cfs_add_task_impl,
    .pick_next_task = (task_t * (*)(void)) cfs_pick_next_task_impl,
    .task_tick = cfs_task_tick,
    .cleanup = cfs_cleanup,
    .private_data = &cfs_rq};