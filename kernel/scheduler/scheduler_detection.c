#include "scheduler_types.h"
#include <ir0/print.h>
#include <ir0/panic/panic.h>

// Forward declarations for scheduler implementations
extern scheduler_ops_t cfs_scheduler_ops;
extern scheduler_ops_t priority_scheduler_ops;
extern scheduler_ops_t roundrobin_scheduler_ops;

// External memory info
extern uint32_t free_pages_count;

// Global state flags
scheduler_ops_t current_scheduler;
scheduler_type_t active_scheduler_type = SCHEDULER_NONE;


// Este es el criterio con el que elijo el planificador que uso. Es como el semáforo si querés verlo así.
scheduler_type_t detect_best_scheduler(void)
{
    // Check if we have enough memory/CPU features for sophisticated schedulers :-)

    // 1. Try CFS (needs more memory for rb-trees) - PRIORITIZED
    extern uint32_t free_pages_count;
    if (free_pages_count > 100)
    { // Reduced threshold to prioritize CFS
        LOG_OK("[SCHED] Sufficient memory for CFS");
        return SCHEDULER_CFS;
    }

    // 2. Try Priority scheduler (medium memory usage)
    if (free_pages_count > 50)
    {
        LOG_OK("[SCHED] Using priority scheduler");
        return SCHEDULER_PRIORITY;
    }

    // 3. Fallback to round-robin (minimal memory)
    LOG_WARN("[SCHED] Low memory, using round-robin fallback");
    return SCHEDULER_ROUND_ROBIN;
}

// Este es uno de esos experimentos que se me ocurrieron en el kernel. La idea es que el sistema detecta en tiempo de ejecucion 
// el sched que te conviene según la cantidad de memo paginada libre. Literalmente es meritocracia kernelística.

int scheduler_cascade_init(void)
{
    print_colored("=== SCHEDULER CASCADE DETECTION ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    active_scheduler_type = detect_best_scheduler();

    // Try to initialize the detected scheduler
    switch (active_scheduler_type)
    {
    case SCHEDULER_CFS:
        current_scheduler = cfs_scheduler_ops;
        break;

    case SCHEDULER_PRIORITY:
        current_scheduler = priority_scheduler_ops;
        break;

    case SCHEDULER_ROUND_ROBIN:
        current_scheduler = roundrobin_scheduler_ops;
        break;

    default:
        LOG_ERR("No valid scheduler found!");
        return -1;
    }

    // Try to initialize
    if (current_scheduler.init)
    {
        current_scheduler.init();
        LOG_OK("Scheduler initialized successfully");
        return 0;
    }

    LOG_ERR("Scheduler initialization failed!");
    return -2;
}

void scheduler_fallback_to_next(void)
{
    LOG_WARN("Current scheduler failed, falling back...");

    switch (active_scheduler_type)
    {
    case SCHEDULER_CFS:
        // CFS failed, try priority
        LOG_WARN("CFS failed, trying priority scheduler");
        active_scheduler_type = SCHEDULER_PRIORITY;
        current_scheduler = priority_scheduler_ops;
        break;

    case SCHEDULER_PRIORITY:
        // Priority failed, try round-robin
        LOG_WARN("Priority scheduler failed, using round-robin");
        active_scheduler_type = SCHEDULER_ROUND_ROBIN;
        current_scheduler = roundrobin_scheduler_ops;
        break;

    case SCHEDULER_ROUND_ROBIN:
        // Round-robin is our last resort
        LOG_ERR("Round-robin scheduler failed! System unusable!");
        panic("All schedulers failed - system cannot continue");
        return;

    default:
    
        panic("Unknown scheduler state in fallback.");
    }

    // Try to initialize the fallback scheduler
    if (current_scheduler.init)
    {
        current_scheduler.init();
        LOG_OK("Fallback scheduler initialized");
    }
    else
    {
        scheduler_fallback_to_next(); // Recursive fallback in real time.
    }
}