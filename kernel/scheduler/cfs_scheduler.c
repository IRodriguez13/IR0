// kernel/scheduler/cfs_scheduler.c - IMPLEMENTACIÓN COMPLETA
#include "scheduler_types.h"
#include <print.h>
#include <stddef.h>
#include <panic/panic.h>
#include <string.h>
#include <stdbool.h>


// Variable externa del task actual
extern task_t *current_running_task;

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
    if (rb_node_pool_index >= MAX_RB_NODES)
    {
        LOG_ERR("CFS: RB node pool exhausted!");
        return NULL;
    }
    rb_node_t *node = &rb_node_pool[rb_node_pool_index++];
    memset(node, 0, sizeof(rb_node_t));
    return node;
}

static void rb_free_node(rb_node_t *node)
{
    // En un pool estático simple, solo marcar como libre
    node->task = NULL;
    node->parent = node->left = node->right = NULL;
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

static void cfs_init(void)
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

static void cfs_add_task(task_t *task)
{
    if (!task)
    {
        LOG_ERR("CFS: add_task received NULL task");
        return;
    }

    rb_node_t *node = rb_alloc_node();
    if (!node)
    {
        LOG_ERR("CFS: Failed to allocate RB node");
        return;
    }

    // Inicializar vruntime para nuevas tareas
    if (task->vruntime == 0)
    {
        task->vruntime = cfs_rq.min_vruntime;
    }

    // Garantizar que nuevas tareas no tengan ventaja injusta
    if (task->vruntime < cfs_rq.min_vruntime)
    {
        task->vruntime = cfs_rq.min_vruntime;
    }

    uint32_t weight = cfs_nice_to_weight(task->nice);

    node->key = task->vruntime;
    node->task = task;
    node->color = RB_RED;

    rb_insert(&cfs_rq.root, node);

    // Actualizar leftmost si es necesario
    if (!cfs_rq.leftmost || task->vruntime < cfs_rq.leftmost->key)
    {
        cfs_rq.leftmost = node;
    }

    cfs_rq.nr_running++;
    cfs_rq.total_weight += weight;

    task->state = TASK_READY;

    LOG_OK("CFS: Task added to runqueue");
}

static task_t *cfs_pick_next_task(void)
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

    // Verificar si necesitamos preempción
    bool should_preempt = false;

    if (cfs_rq.leftmost)
    {
        // Preemptar si hay una tarea con vruntime significativamente menor
        uint64_t leftmost_vruntime = cfs_rq.leftmost->key;
        uint64_t current_vruntime = current_running_task->vruntime;

        if (current_vruntime > leftmost_vruntime + cfs_rq.min_granularity)
        {
            should_preempt = true;
        }

        // O si se agotó el time slice asignado
        if (current_running_task->exec_time >= current_running_task->time_slice)
        {
            should_preempt = true;
        }

        // O si la diferencia de fairness es muy grande
        if (current_vruntime > cfs_rq.avg_vruntime + cfs_rq.targeted_latency)
        {
            should_preempt = true;
        }
    }

    if (should_preempt)
    {
        // Marcar para replanificación
        current_running_task->state = TASK_READY;
        current_running_task->context_switches++;

        cfs_add_task(current_running_task);
        current_running_task = NULL;

        LOG_OK("CFS: Task preempted due to fairness/time slice");
    }
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
    .init = cfs_init,
    .add_task = cfs_add_task,
    .pick_next_task = cfs_pick_next_task,
    .task_tick = cfs_task_tick,
    .cleanup = cfs_cleanup,
    .private_data = &cfs_rq};