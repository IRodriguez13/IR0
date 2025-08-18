// ===============================================================================
// SCHEDULER CENTRAL IMPLEMENTATION WITH REAL FUNCTIONALITY
// ===============================================================================

#include "scheduler.h"
#include "scheduler_types.h"
#include "task.h"
#include "../../includes/ir0/print.h"
#include "../../arch/common/arch_interface.h"
#include <string.h>

// ===============================================================================
// CONSTANTS AND DEFINITIONS
// ===============================================================================

#define DEFAULT_QUANTUM 10
#define MAX_PRIORITY_LEVELS 256
#define TASK_STACK_SIZE (4 * 1024)

// Task states
#define TASK_NEW 0
#define TASK_READY 1
#define TASK_RUNNING 2
#define TASK_SLEEPING 3
#define TASK_DEAD 4

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
    print("Initializing IR0 Scheduler\n");

    // Initialize scheduler state
    memset(&scheduler_state, 0, sizeof(scheduler_state_t));
    scheduler_state.initialized = 1;
    scheduler_state.current_task = NULL;
    scheduler_state.next_task = NULL;
    scheduler_state.scheduler_type = SCHEDULER_ROUND_ROBIN;
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
            // Por ahora, solo simulamos la ejecución
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
        return;
    }

    // Set task state
    task->state = TASK_READY;
    task->last_run_time = scheduler_state.tick_count;

    // Add to appropriate queue based on scheduler type
    switch (scheduler_state.scheduler_type)
    {
    case SCHEDULER_ROUND_ROBIN:
        round_robin_add_task(task);
        break;

    case SCHEDULER_PRIORITY:
        priority_add_task(task);
        break;

    case SCHEDULER_CFS:
        cfs_add_task(task);
        break;

    default:
        break;
    }
}

void scheduler_tick(void)
{
    if (!scheduler_state.running)
    {
        return;
    }

    scheduler_state.tick_count++;

    // Wake up sleeping tasks
    scheduler_wake_sleeping_tasks();

    // Check if current task should yield
    if (scheduler_state.current_task)
    {
        uint32_t time_slice = scheduler_get_time_slice(scheduler_state.current_task);

        if (scheduler_state.tick_count - scheduler_state.current_task->last_run_time >= time_slice)
        {
            // Task has used its time slice, yield
            scheduler_yield();
        }
    }
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

    // Check all sleeping queues
    for (int i = 0; i < MAX_PRIORITY_LEVELS; i++)
    {
        task_t *task = scheduler_state.sleeping_queues[i];
        task_t *prev = NULL;

        while (task)
        {
            if (scheduler_state.tick_count >= task->last_run_time)
            {
                // Wake up task
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

                task = next;
            }
            else
            {
                prev = task;
                task = task->next;
            }
        }
    }
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
    if (!new_task)
    {
        return;
    }

    task_t *old_task = scheduler_state.current_task;

    // Update task states
    if (old_task)
    {
        old_task->state = TASK_READY;
        old_task->total_runtime += scheduler_state.tick_count - old_task->last_run_time;
    }

    new_task->state = TASK_RUNNING;
    new_task->last_run_time = scheduler_state.tick_count;

    // Update scheduler state
    scheduler_state.current_task = new_task;
    scheduler_state.next_task = NULL;

    // Perform context switch
    if (old_task != new_task)
    {
        // TODO: Implement actual context switch
        // switch_context(old_task, new_task);
    }
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
// SCHEDULER ALGORITHM IMPLEMENTATIONS (STUBS)
// ===============================================================================

int round_robin_init(void)
{
    // TODO: Implement Round Robin initialization
    return 0;
}

int round_robin_add_task(task_t *task)
{
    // TODO: Implement Round Robin add task
    return 0;
}

int round_robin_remove_task(task_t *task)
{
    // TODO: Implement Round Robin remove task
    return 0;
}

task_t *round_robin_get_next_task(void)
{
    // TODO: Implement Round Robin get next task
    return NULL;
}

int priority_init(void)
{
    // TODO: Implement Priority initialization
    return 0;
}

int priority_add_task(task_t *task)
{
    // TODO: Implement Priority add task
    return 0;
}

int priority_remove_task(task_t *task)
{
    // TODO: Implement Priority remove task
    return 0;
}

task_t *priority_get_next_task(void)
{
    // TODO: Implement Priority get next task
    return NULL;
}

int cfs_init(void)
{
    // TODO: Implement CFS initialization
    return 0;
}

int cfs_add_task(task_t *task)
{
    // TODO: Implement CFS add task
    return 0;
}

int cfs_remove_task(task_t *task)
{
    // TODO: Implement CFS remove task
    return 0;
}

task_t *cfs_get_next_task(void)
{
    // TODO: Implement CFS get next task
    return NULL;
}

uint32_t cfs_get_time_slice(task_t *task)
{
    // TODO: Implement CFS get time slice
    return scheduler_state.quantum;
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
    // TODO: Implement dispatch loop
}

scheduler_type_t get_active_scheduler(void)
{
    return scheduler_state.scheduler_type;
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
