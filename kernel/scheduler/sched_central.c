// ===============================================================================
// SCHEDULER CENTRAL IMPLEMENTATION
// ===============================================================================

#include "scheduler.h"
#include "scheduler_types.h"
#include "task.h"
#include <stdbool.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include <arch_interface.h>
#include <string.h>

// Forward declarations
extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
extern void idle_task_function(void *arg);

// CFS real function declarations (will be made non-static)
extern void cfs_init_impl(void);
extern void cfs_add_task_impl(task_t *task);
extern void cfs_remove_task_impl(task_t *task);
extern task_t *cfs_pick_next_task_impl(void);

// ===============================================================================
// CONSTANTS AND DEFINITIONS
// ===============================================================================

#define DEFAULT_QUANTUM 10
#define MAX_PRIORITY_LEVELS 256
#define TASK_STACK_SIZE (4 * 1024)

// CFS constants
#define CFS_TARGETED_LATENCY 20000000ULL // 20ms en nanosegundos
#define CFS_MIN_GRANULARITY 4000000ULL   // 4ms mínimo por proceso

// Task states
#define TASK_NEW 0
#define TASK_READY 1
#define TASK_RUNNING 2
#define TASK_SLEEPING 3
#define TASK_DEAD 4

// Architecture-aware interrupt protection
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

// ===============================================================================
// SCHEDULER STATE STRUCTURE
// ===============================================================================

typedef struct
{
    int initialized;
    int running;
    task_t *current_task;
    task_t *next_task;
    scheduler_type_t scheduler_type;
    uint32_t quantum;
    uint32_t tick_count;

    // Task queues
    task_t *ready_queues[MAX_PRIORITY_LEVELS];
    task_t *sleeping_queues[MAX_PRIORITY_LEVELS];
} scheduler_state_t;

// ===============================================================================
// GLOBAL VARIABLES
// ===============================================================================

static scheduler_state_t scheduler_state;

// ===============================================================================
// FORWARD DECLARATIONS
// ===============================================================================

// Round Robin functions
int round_robin_init(void);
int round_robin_add_task(task_t *task);
int round_robin_remove_task(task_t *task);
task_t *round_robin_get_next_task(void);

// Priority functions
int priority_init(void);
int priority_add_task(task_t *task);
int priority_remove_task(task_t *task);
task_t *priority_get_next_task(void);

// CFS functions
int cfs_init(void);
int cfs_add_task(task_t *task);
int cfs_remove_task(task_t *task);
task_t *cfs_get_next_task(void);
uint32_t cfs_get_time_slice(task_t *task);

// Scheduler helper functions
void scheduler_wake_sleeping_tasks(void);
void scheduler_yield(void);
task_t *scheduler_get_next_task(void);
void scheduler_switch_task(task_t *new_task);
uint32_t scheduler_get_time_slice(task_t *task);

// ===============================================================================
// SCHEDULER STATISTICS STRUCTURE
// ===============================================================================

typedef struct
{
    scheduler_type_t scheduler_type;
    uint32_t quantum;
    uint32_t tick_count;
    int running;
    uint32_t ready_task_count;
    uint32_t sleeping_task_count;
} scheduler_stats_t;

// ===============================================================================
// MAIN SCHEDULER FUNCTIONS
// ===============================================================================

void scheduler_init(void)
{

    // Initialize scheduler state
    memset(&scheduler_state, 0, sizeof(scheduler_state_t));
    scheduler_state.initialized = 1;
    scheduler_state.current_task = NULL;
    scheduler_state.next_task = NULL;
    scheduler_state.scheduler_type = SCHEDULER_CFS; // Cambiar a CFS por defecto
    scheduler_state.quantum = DEFAULT_QUANTUM;
    scheduler_state.tick_count = 0;
    scheduler_state.running = 0;

    // Initialize task queues
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++)
    {
        scheduler_state.ready_queues[i] = NULL;
        scheduler_state.sleeping_queues[i] = NULL;
    }

    // Initialize scheduler algorithms
    if (round_robin_init() != 0)
    {
        print_error("Failed to initialize Round Robin scheduler\n");
        return;
    }

    if (priority_init() != 0)
    {
        print_error("Failed to initialize Priority scheduler\n");
        return;
    }

    if (cfs_init() != 0)
    {
        print_error("Failed to initialize CFS scheduler\n");
        return;
    }

    // Create idle task
    extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
    extern void idle_task_function(void *arg);

    // Get or create idle task
    task_t *idle_task = get_idle_task();
    if (!idle_task)
    {
        print_error("Failed to get idle task!\n");
        return;
    }

    // Add idle task to scheduler
    add_task(idle_task);

    print_success("Scheduler initialized successfully\n");
}

void scheduler_start(void)
{
    if (scheduler_state.running)
    {
        return; // Already running
    }

    print("Starting IR0 Scheduler\n");

    scheduler_state.running = 1;
    scheduler_state.tick_count = 0;

    print_success("Scheduler started successfully\n");
}

// Función principal del scheduler - NUNCA RETORNA
void scheduler_main_loop(void)
{
    if (!scheduler_state.running)
    {
        panic("Scheduler not running!");
    }

    print("Entering scheduler main loop...\n");

    for (;;)
    {
        // Obtener siguiente tarea
        task_t *next_task = scheduler_get_next_task();

        if (next_task && next_task != get_idle_task())
        {
            // Hay una tarea real para ejecutar
            scheduler_switch_task(next_task);

            // Ejecutar la tarea (esto debería hacer context switch)
            if (next_task->entry)
            {
                next_task->entry(next_task->entry_arg);
            }
        }
        else
        {
            // No hay tareas, ejecutar idle task
            task_t *idle_task = get_idle_task();
            if (idle_task)
            {
                scheduler_switch_task(idle_task);
                // El idle task hace HLT
                idle_task->entry(idle_task->entry_arg);
            }
            else
            {
                // Fallback: HLT directo
                cpu_wait();
            }
        }
    }
}

void add_task(task_t *task)
{
    if (!task)
    {
        LOG_ERR("add_task: task is NULL");
        return;
    }

    // CRITICAL: Disable interrupts for atomic operation
    uint32_t flags = interrupt_save_and_disable();

    // Validate task state before adding
    if (task->state == TASK_TERMINATED)
    {
        interrupt_restore(flags);
        LOG_WARN("add_task: Ignoring terminated task PID ");
        print_hex_compact(task->pid);
        print("\n");
        return;
    }

    // Set task state
    task->state = TASK_READY;
    task->last_run_time = scheduler_state.tick_count;

    // Validate scheduler state
    if (!scheduler_state.initialized)
    {
        interrupt_restore(flags);
        LOG_ERR("add_task: Scheduler not initialized!");
        return;
    }

    // Add to appropriate queue based on scheduler type with error handling
    int result = -1;
    switch (scheduler_state.scheduler_type)
    {
    case SCHEDULER_ROUND_ROBIN:
        result = round_robin_add_task(task);
        break;

    case SCHEDULER_PRIORITY:
        result = priority_add_task(task);
        break;

    case SCHEDULER_CFS:
        result = cfs_add_task(task);
        break;

    default:
        interrupt_restore(flags);
        LOG_ERR("add_task: Unknown scheduler type ");
        print_hex_compact(scheduler_state.scheduler_type);
        print("\n");
        return;
    }

    interrupt_restore(flags);

    // Check if task addition was successful
    if (result != 0)
    {
        LOG_ERR("add_task: Failed to add task to scheduler (result: ");
        print_hex_compact(result);
        print(")\n");

        // Try fallback to idle task if available
        task_t *idle = get_idle_task();
        if (idle && task != idle)
        {
            LOG_WARN("add_task: Attempting to add task to idle queue as fallback");
            // This is a emergency fallback - not ideal but prevents system hang
        }
    }
    else
    {
        print("SUCCESS: Task PID ");
        print_hex_compact(task->pid);
        print(" added to ");
        print(get_scheduler_name());
        print(" scheduler\n");
    }
}

static void scheduler_health_check(void)
{
    static uint32_t last_health_check = 0;

    // Run health check every 1000 ticks (approximately 1 second)
    if (scheduler_state.tick_count - last_health_check < 1000)
    {
        return;
    }

    last_health_check = scheduler_state.tick_count;

    print("SCHEDULER: Health check at tick ");
    print_hex_compact(scheduler_state.tick_count);
    print("\n");

    // Check for common problems

    //  Check if scheduler is stuck
    static uint32_t last_context_switch_count = 0;
    uint32_t total_context_switches = 0;

    if (scheduler_state.current_task)
    {
        total_context_switches = scheduler_state.current_task->context_switches;
    }

    if (total_context_switches == last_context_switch_count)
    {
        LOG_WARN("SCHEDULER: Possible scheduler stall detected");
    }
    last_context_switch_count = total_context_switches;

    //  Check for memory leaks in task management
    uint32_t active_tasks = get_task_count();
    if (active_tasks > 1000)
    {
        LOG_WARN("SCHEDULER: High task count detected: ");
        print_hex_compact(active_tasks);
        print("\n");
    }

}

void scheduler_tick(void)
{
    if (!scheduler_state.running || !scheduler_state.initialized)
    {
        return;
    }

    uint32_t flags = interrupt_save_and_disable();

    scheduler_state.tick_count++;

    // Wake up sleeping tasks
    scheduler_wake_sleeping_tasks();

    // Check if current task should yield with better logic
    if (scheduler_state.current_task)
    {
        // Validate current task state
        if (scheduler_state.current_task->state == TASK_TERMINATED)
        {
            LOG_WARN("scheduler_tick: Current task is terminated, switching");
            scheduler_state.current_task = NULL;
            interrupt_restore(flags);
            scheduler_yield();
            return;
        }

        uint32_t time_slice = scheduler_get_time_slice(scheduler_state.current_task);
        uint32_t runtime = scheduler_state.tick_count - scheduler_state.current_task->last_run_time;

        // Multiple preemption conditions
        bool should_yield = false;
        const char *yield_reason = "unknown";

        // 1. Time slice exhausted
        if (runtime >= time_slice)
        {
            should_yield = true;
            yield_reason = "time slice exhausted";
            print("SCHEDULER: Time slice exhausted for PID ");
            print_hex_compact(scheduler_state.current_task->pid);
            print(" (runtime: ");
            print_hex_compact(runtime);
            print(", slice: ");
            print_hex_compact(time_slice);
            print(")\n");
        }

        // 2. Task explicitly yielded
        if (scheduler_state.current_task->state == TASK_READY)
        {
            should_yield = true;
            yield_reason = "voluntary yield";
            print("SCHEDULER: Task yielded voluntarily\n");
        }

        // 3. Higher priority task available (for priority scheduler only)
        if (!should_yield && scheduler_state.scheduler_type == SCHEDULER_PRIORITY)
        {
            // Peek at next task without removing it from queue
            task_t *peek_next = NULL;

            // Use scheduler-specific peek function to avoid removing task
            switch (scheduler_state.scheduler_type)
            {
            case SCHEDULER_PRIORITY:
                // For priority scheduler, check if higher priority tasks are available
                // This is a simplified check - in real implementation, you'd peek at priority queues
                peek_next = priority_get_next_task();
                if (peek_next && peek_next != scheduler_state.current_task)
                {
                    if (peek_next->priority > scheduler_state.current_task->priority)
                    {
                        should_yield = true;
                        yield_reason = "higher priority task available";
                        print("SCHEDULER: Higher priority task PID ");
                        print_hex_compact(peek_next->pid);
                        print(" available (priority ");
                        print_hex_compact(peek_next->priority);
                        print(" > ");
                        print_hex_compact(scheduler_state.current_task->priority);
                        print(")\n");
                    }
                    // IMPORTANT: Re-add the peeked task back to the queue
                    // since we're not switching to it yet
                    priority_add_task(peek_next);
                }
                break;
            default:
                break;
            }
        }

        // 4. Check for task state changes (blocking, etc.)
        if (!should_yield && scheduler_state.current_task->state == TASK_BLOCKED)
        {
            should_yield = true;
            yield_reason = "task blocked";
            print("SCHEDULER: Current task blocked\n");
        }

        if (should_yield)
        {
            print("SCHEDULER: Yielding due to: ");
            print(yield_reason);
            print("\n");

            interrupt_restore(flags);
            scheduler_yield();
            return;
        }

        // If no yield condition met, update task runtime stats
        scheduler_state.current_task->total_runtime += 1; // 1 tick increment
    }
    else
    {
        // No current task - try to get one
        print("SCHEDULER: No current task, yielding to get next task\n");
        interrupt_restore(flags);
        scheduler_yield();
        return;
    }

    interrupt_restore(flags);
}

// ===============================================================================
// HELPER FUNCTIONS
// ===============================================================================

void scheduler_wake_sleeping_tasks(void)
{
    if (!scheduler_state.running)
    {
        return;
    }

    static uint32_t wake_check_counter = 0;
    wake_check_counter++;

    // Check sleeping tasks every 10 ticks to reduce overhead
    if (wake_check_counter % 10 != 0)
    {
        return;
    }

    uint32_t tasks_woken = 0;
    uint32_t current_tick = scheduler_state.tick_count;

    // Check all sleeping queues
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++)
    {
        task_t *task = scheduler_state.sleeping_queues[i];
        task_t *prev = NULL;

        while (task)
        {
            // Calculate how long task has been sleeping
            uint32_t sleep_time = current_tick - task->last_run_time;

            // Wake up task if it has slept long enough
            // For now, wake up tasks that have slept for more than 100 ticks
            if (sleep_time >= 100)
            {
                task_t *next = task->next;

                // Remove from sleeping queue
                if (prev)
                {
                    prev->next = next;
                }
                else
                {
                    scheduler_state.sleeping_queues[i] = next;
                }

                // Add back to ready queue
                task->state = TASK_READY;
                task->next = NULL;
                add_task(task);

                tasks_woken++;

                print("SCHEDULER: Woke up task PID ");
                print_hex_compact(task->pid);
                print(" after ");
                print_hex_compact(sleep_time);
                print(" ticks\n");

                task = next;
            }
            else
            {
                prev = task;
                task = task->next;
            }
        }
    }

    if (tasks_woken > 0)
    {
        print("SCHEDULER: Woke up ");
        print_hex_compact(tasks_woken);
        print(" sleeping tasks\n");
    }

    // Run periodic health check
    scheduler_health_check();
}

void scheduler_yield(void)
{
    if (!scheduler_state.running)
    {
        return;
    }

    // Get next task
    task_t *next_task = scheduler_get_next_task();
    if (!next_task)
    {
        return;
    }

    // Switch to next task
    scheduler_switch_task(next_task);
}

task_t *scheduler_get_next_task(void)
{
    if (!scheduler_state.running)
    {
        return NULL;
    }

    // Get next task based on scheduler type
    switch (scheduler_state.scheduler_type)
    {
    case SCHEDULER_ROUND_ROBIN:
        return round_robin_get_next_task();

    case SCHEDULER_PRIORITY:
        return priority_get_next_task();

    case SCHEDULER_CFS:
        return cfs_get_next_task();

    default:
        return NULL;
    }
}

void scheduler_switch_task(task_t *new_task)
{
    // ===============================================================================
    // 1. VALIDACIONES INICIALES
    // ===============================================================================

    if (!new_task)
    {
        LOG_ERR("scheduler_switch_task: new_task is NULL");
        return;
    }

    task_t *old_task = scheduler_state.current_task;

    // Early return si es la misma tarea
    if (old_task == new_task)
    {
        print("scheduler_switch_task: Same task, no switch needed");
        return;
    }

    // ===============================================================================
    // 2. MANEJO DE TAREAS TERMINADAS O INVÁLIDAS
    // ===============================================================================

    if (old_task && old_task->state == TASK_TERMINATED)
    {
        LOG_WARN("scheduler_switch_task: Current task is terminated, using idle task as placeholder");

        // CRÍTICO: El assembly switch_context_x64 necesita un contexto válido para old_task
        // No podemos pasar NULL, así que usamos idle_task como placeholder seguro
        old_task = get_idle_task();
        
        // Verificación adicional del idle task
        if (!old_task)
        {
            panic("scheduler_switch_task: Idle task is NULL - system corrupted");
        }
    }
    
    // Si old_task sigue siendo NULL (primera ejecución), usar idle task
    if (!old_task)
    {
        old_task = get_idle_task();
        
        if (!old_task)
        {
            panic("scheduler_switch_task: Cannot get idle task - system corrupted");
        }
    }

    // ===============================================================================
    // 3. VALIDACIONES DE STACK Y ESTADO DE LA NUEVA TAREA
    // ===============================================================================

    // Validar que new_task tiene stack válido
    if (new_task->rsp == 0)
    {
        LOG_ERR("scheduler_switch_task: New task has invalid stack pointer (RSP=0)");
        return;
    }

    // Validar que new_task no está en estado inválido
    if (new_task->state == TASK_TERMINATED)
    {
        LOG_ERR("scheduler_switch_task: Cannot switch to terminated task PID");
        return;
    }

    // Validación adicional: verificar que el stack está en rango válido
    if (new_task->stack_base && new_task->stack_size > 0)
    {
        uintptr_t stack_start = (uintptr_t)new_task->stack_base;
        uintptr_t stack_end = stack_start + new_task->stack_size;
        
        if (new_task->rsp < stack_start || new_task->rsp >= stack_end)
        {
            LOG_ERR("scheduler_switch_task: New task stack pointer out of bounds");
            return;
        }
    }


    // ===============================================================================
    // 5. ACTUALIZAR ESTADÍSTICAS ANTES DEL SWITCH
    // ===============================================================================

    // Actualizar old_task stats ANTES del switch
    if (old_task && old_task->state != TASK_TERMINATED)
    {
        uint64_t current_time = scheduler_state.tick_count;
        uint64_t runtime = current_time - old_task->last_run_time;

        old_task->total_runtime += runtime;
        
        // Solo cambiar estado si no es idle task
        if (old_task != get_idle_task())
        {
            old_task->state = TASK_READY; // Será agregado de vuelta a ready queue
        }

    }

    // ===============================================================================
    // 6. PREPARAR NEW_TASK PARA EJECUCIÓN
    // ===============================================================================

    new_task->state = TASK_RUNNING;
    new_task->last_run_time = scheduler_state.tick_count;
    new_task->context_switches++;

    // ===============================================================================
    // 7. ACTUALIZAR SCHEDULER STATE
    // ===============================================================================

    scheduler_state.current_task = new_task;
    scheduler_state.next_task = NULL;

    // ===============================================================================
    // 8. VALIDACIÓN FINAL ANTES DEL CONTEXT SWITCH
    // ===============================================================================
    
    // Última verificación de integridad
    if (!old_task || !new_task)
    {
        panic("scheduler_switch_task: NULL task in context switch");
    }
    
    if (old_task->rsp == 0 && old_task != get_idle_task())
    {
        LOG_WARN("scheduler_switch_task: Old task has invalid RSP, but proceeding");
    }

    // ===============================================================================
    // 9. PERFORM HARDWARE CONTEXT SWITCH
    // ===============================================================================


    // Verificar que la función existe (safety check en debug builds)
    #ifdef DEBUG
    extern void switch_context_x64(task_t *current, task_t *next);
    if (!switch_context_x64)
    {
        panic("scheduler_switch_task: switch_context_x64 function not available");
    }
    #endif

    // IMPORTANTE: Después de esta llamada, ejecutaremos en el contexto de new_task
    // Todo el código que sigue se ejecutará en la nueva tarea cuando sea preempted
    extern void switch_context_x64(task_t *current, task_t *next);
    switch_context_x64(old_task, new_task);

    // ===============================================================================
    // 10. POST-SWITCH CODE (se ejecuta cuando la tarea retorna al scheduler)
    // ===============================================================================
    
    // Este código se ejecuta cuando la tarea RETORNA al scheduler
    // (por ejemplo, después de un yield o preemption)
    
    // Opcional: verificaciones post-switch para debugging
    #ifdef DEBUG_SCHEDULER
    if (scheduler_state.current_task && scheduler_state.current_task->state != TASK_RUNNING)
    {
        LOG_WARN("scheduler_switch_task: Current task state inconsistent after switch");
    }
    #endif
}



uint32_t scheduler_get_time_slice(task_t *task)
{
    if (!task)
    {
        return scheduler_state.quantum;
    }

    // Calculate time slice based on scheduler type and task priority
    switch (scheduler_state.scheduler_type)
    {
    case SCHEDULER_ROUND_ROBIN:
        return scheduler_state.quantum;

    case SCHEDULER_PRIORITY:
        return scheduler_state.quantum * (MAX_PRIORITY_LEVELS - task->priority);

    case SCHEDULER_CFS:
        return cfs_get_time_slice(task);

    default:
        return scheduler_state.quantum;
    }
}

// ===============================================================================
// IDLE TASK MANAGEMENT
// ===============================================================================

static task_t idle_task;
static bool idle_task_initialized = false;

task_t *get_idle_task(void)
{
    if (!idle_task_initialized)
    {
        memset(&idle_task, 0, sizeof(task_t));
        idle_task.pid = 0;
        idle_task.state = TASK_READY;
        idle_task.priority = 0;
        idle_task_initialized = true;
        print("IDLE: Idle task initialized\n");
    }
    return &idle_task;
}

// ===============================================================================
// SCHEDULER ALGORITHM IMPLEMENTATIONS (STUBS)
// ===============================================================================

// Round Robin implementation
static task_t *round_robin_queue = NULL;

int round_robin_init(void)
{
    round_robin_queue = NULL;
    return 0;
}

int round_robin_add_task(task_t *task)
{
    if (!task)
        return -1;

    if (!round_robin_queue)
    {
        round_robin_queue = task;
        task->next = task; // Circular list
    }
    else
    {
        task->next = round_robin_queue->next;
        round_robin_queue->next = task;
        round_robin_queue = task;
    }
    return 0;
}

int round_robin_remove_task(task_t *task)
{
    if (!task || !round_robin_queue)
        return -1;

    if (round_robin_queue == round_robin_queue->next)
    {
        // Only one task
        round_robin_queue = NULL;
    }
    else
    {
        // Find and remove task
        task_t *current = round_robin_queue;
        do
        {
            if (current->next == task)
            {
                current->next = task->next;
                if (round_robin_queue == task)
                {
                    round_robin_queue = current;
                }
                break;
            }
            current = current->next;
        } 
        while (current != round_robin_queue);
    }
    return 0;
}

task_t *round_robin_get_next_task(void)
{
    if (!round_robin_queue)
        return NULL;

    // Get next task and advance queue
    task_t *next = round_robin_queue->next;
    round_robin_queue = next;

    return next;
}

int priority_init(void)
{
    // TODO: Implement Priority initialization
    return 0;
}

int priority_add_task(task_t *task)
{
    (void)task; // Parameter not used yet
    // TODO: Implement Priority add task
    return 0;
}

int priority_remove_task(task_t *task)
{
    (void)task; // Parameter not used yet
    // TODO: Implement Priority remove task
    return 0;
}

task_t *priority_get_next_task(void)
{
    // TODO: Implement Priority get next task
    return NULL;
}

// CFS implementation - wrapper functions
int cfs_init(void)
{
    // Call the real CFS init function
    cfs_init_impl();
    return 0;
}

int cfs_add_task(task_t *task)
{
    // Call the real CFS add task function
    cfs_add_task_impl(task);
    return 0;
}

int cfs_remove_task(task_t *task)
{
    // Call the real CFS remove task function
    cfs_remove_task_impl(task);
    return 0;
}

task_t *cfs_get_next_task(void)
{
    // Call the real CFS pick next task function
    return cfs_pick_next_task_impl();
}

uint32_t cfs_get_time_slice(task_t *task)
{
    // Calculate time slice based on CFS algorithm
    if (!task)
        return scheduler_state.quantum;

    // CFS time slice calculation
    uint32_t weight = 1024;       // Default weight for nice 0
    uint32_t total_weight = 1024; // Simplified

    uint64_t time_slice = (CFS_TARGETED_LATENCY * weight) / total_weight;

    // Ensure minimum granularity
    if (time_slice < CFS_MIN_GRANULARITY)
        time_slice = CFS_MIN_GRANULARITY;

    return (uint32_t)time_slice;
}

// ===============================================================================
// MEMORY ALLOCATION STUBS
// ===============================================================================

// Functions moved to heap_allocator.c to avoid duplication

// ===============================================================================
// ADDITIONAL FUNCTIONS FOR COMPATIBILITY
// ===============================================================================

void scheduler_dispatch_loop(void)
{
    print("scheduler_dispatch_loop: ENTRY\n");
    print_colored("=== ENTERING SCHEDULER DISPATCH LOOP ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("Shell exited, kernel now running scheduler dispatch loop\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_colored("System will run until next interrupt or system call\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    // Usar el scheduler detectado automáticamente
    extern scheduler_ops_t current_scheduler;
    extern scheduler_type_t active_scheduler_type;

    // Flag para controlar mensajes de idle (solo mostrar una vez)
    static int idle_message_shown = 0;

    // Flag para controlar si volver a la shell
    static int return_to_shell = 0;

    // Incluir funciones del teclado para despertar del idle
    extern void set_idle_mode(int is_idle);
    extern int is_wake_requested(void);
    extern void clear_wake_request(void);

    print("Active scheduler: ");
    switch (active_scheduler_type)
    {
    case SCHEDULER_CFS:
        print("Completely Fair Scheduler (CFS)");
        break;
    case SCHEDULER_PRIORITY:
        print("Priority-based Scheduler");
        break;
    case SCHEDULER_ROUND_ROBIN:
        print("Round Robin Scheduler");
        break;
    default:
        print("Unknown Scheduler");
        break;
    }
    print("\n");

    // Obtener o crear idle task si no existe
    task_t *idle_task = get_idle_task();
    if (!idle_task)
    {
        print_warning("No idle task found, creating one...\n");

        // Crear idle task si no existe
        extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
        extern void idle_task_function(void *arg);

        idle_task = create_task(idle_task_function, NULL, 0, 0);
        if (!idle_task)
        {
            print_warning("Failed to create idle task, using CPU wait fallback\n");
        }
        else
        {
            add_task(idle_task);
            print_success("Idle task created and added to scheduler\n");
        }
    }

    if (idle_task)
    {
        print_success("Idle task ready\n");
    }

    // Loop principal del dispatcher usando el scheduler detectado
    for (;;)
    {
        // Verificar si hay solicitud de volver a la shell
        if (return_to_shell)
        {
            return_to_shell = 0;
            print_colored("🔄 Returning to shell\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            extern void shell_start(void);
            shell_start();
            // Si llegamos aquí, la shell terminó de nuevo, continuar con dispatch loop
            idle_message_shown = 0; // Reset para mostrar mensaje de nuevo si vuelve a idle
        }

        // Obtener siguiente tarea usando el scheduler activo
        task_t *next_task = NULL;
        if (current_scheduler.pick_next_task)
        {
            next_task = current_scheduler.pick_next_task();
        }

        if (next_task && next_task != idle_task)
        {
            // Hay una tarea real para ejecutar
            print("Dispatching task PID: ");
            print_uint32(next_task->pid);
            print("\n");

            scheduler_switch_task(next_task);

            // Ejecutar la tarea (esto debería hacer context switch)
            if (next_task->entry)
            {
                next_task->entry(next_task->entry_arg);
            }
        }
        else
        {
            // No hay tareas, ejecutar idle task
            if (!idle_message_shown)
            {
                print_colored("🔄 No tasks ready, entering IDLE mode (HLT)\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
                print_colored("System waiting for interrupts...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
                print_colored("Press F12 to wake from idle mode\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
                idle_message_shown = 1;
            }

            // Marcar que estamos en modo idle
            set_idle_mode(1);

            // Verificar si hay solicitud de despertar
            if (is_wake_requested())
            {
                clear_wake_request();
                set_idle_mode(0);
                return_to_shell = 1; // Activar flag para volver a la shell
                continue;            // Volver al inicio del loop
            }

            if (idle_task)
            {
                scheduler_switch_task(idle_task);
                // El idle task hace HLT
                idle_task->entry(idle_task->entry_arg);
            }
            else
            {
                // Fallback: HLT directo
                cpu_wait();
            }
        }
    }
}

scheduler_type_t get_active_scheduler(void)
{
    return scheduler_state.scheduler_type;
}

// Getter function for current task
task_t *get_current_task(void)
{
    return scheduler_state.current_task;
}

// Setter function to clear current task (for sys_exit)
void set_current_task_null(void)
{
    scheduler_state.current_task = NULL;
}

// Function to terminate current task and switch to next
void terminate_current_task(void)
{
    if (!scheduler_state.running)
    {
        return;
    }

    task_t *current_task = scheduler_state.current_task;
    if (!current_task)
    {
        return;
    }

    print("Terminating current task PID: ");
    print_uint32(current_task->pid);
    print("\n");

    // Mark current task as terminated
    current_task->state = TASK_TERMINATED;

    // Get next task (this will skip terminated tasks)
    task_t *next_task = scheduler_get_next_task();
    if (!next_task)
    {
        // No other tasks available, switch to idle
        next_task = get_idle_task();
    }

    if (next_task)
    {
        print("Switching to next task PID: ");
        print_uint32(next_task->pid);
        print("\n");

        // Switch to next task
        scheduler_switch_task(next_task);
    }
    else
    {
        print("No tasks available, entering idle mode\n");
        // Enter idle mode
        extern void cpu_wait(void);
        cpu_wait();
    }
}

const char *get_scheduler_name(void)
{
    switch (scheduler_state.scheduler_type)
    {
    case SCHEDULER_ROUND_ROBIN:
        return "Round Robin";
    case SCHEDULER_PRIORITY:
        return "Priority";
    case SCHEDULER_CFS:
        return "Completely Fair Scheduler";
    default:
        return "Unknown";
    }
}

void force_scheduler_fallback(void)
{
    // TODO: Implement fallback
}

void dump_scheduler_state(void)
{
    print("=== Scheduler State ===\n");
    print("Initialized: ");
    print(scheduler_state.initialized ? "Yes" : "No");
    print("\n");
    print("Running: ");
    print(scheduler_state.running ? "Yes" : "No");
    print("\n");
    print("Scheduler Type: ");
    print(get_scheduler_name());
    print("\n");
    print("Tick count: ");
    print_uint32(scheduler_state.tick_count);
    print("\n");
}

int scheduler_ready(void)
{
    return scheduler_state.initialized && scheduler_state.running;
}
