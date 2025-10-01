#include <stdbool.h>
#include <stddef.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include "scheduler_types.h"


// Only CFS scheduler
extern scheduler_ops_t cfs_scheduler_ops;

// External memory info
extern uint32_t free_pages_count;

// Global state flags
scheduler_ops_t current_scheduler;
scheduler_type_t active_scheduler_type = SCHEDULER_NONE;

static bool run_scheduler_system_test(void)
{

    // Test 1: Can we create a test task?
    extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
    extern void test_task_function(void *arg);

    task_t *test_task = create_task(test_task_function, (void *)999, 1, 0);
    if (!test_task)
    {
        LOG_ERR("SCHEDULER: System test failed - cannot create test task");
        return false;
    }

    // Test 2: Can we add task to scheduler?
    if (current_scheduler.add_task)
    {
        current_scheduler.add_task(test_task);
    }
    else
    {
        LOG_ERR("SCHEDULER: System test failed - no add_task function");
        destroy_task(test_task);
        return false;
    }

    // Test 3: Can we pick next task?
    task_t *picked_task = NULL;
    if (current_scheduler.pick_next_task)
    {
        picked_task = current_scheduler.pick_next_task();
    }
    else
    {
        LOG_ERR("SCHEDULER: System test failed - no pick_next_task function");
        destroy_task(test_task);
        return false;
    }

    // Test 4: Did we get our test task back?
    if (picked_task != test_task)
    {
        LOG_WARN("SCHEDULER: System test - got different task than expected");
        // This might be OK if there are other tasks in the system
    }

    // Cleanup test task
    if (picked_task)
    {
        destroy_task(picked_task);
    }

    return true;
}

static bool validate_scheduler_init(void)
{
    // Basic validation checks
    if (!current_scheduler.init || !current_scheduler.add_task ||
        !current_scheduler.pick_next_task)
    {
        LOG_ERR("SCHEDULER: Missing required function pointers");
        return false;
    }

    if (!current_scheduler.name)
    {
        LOG_ERR("SCHEDULER: Scheduler name not set");
        return false;
    }

    // Acá 
    // Type-specific validation
    switch (active_scheduler_type)
    {
    case SCHEDULER_CFS:
        // Validate CFS-specific structures
        // Add specific checks here if needed
        break;

    // Only CFS supported

    default:
        LOG_ERR("SCHEDULER: Unknown scheduler type in validation");
        return false;
    }

    return true;
}

// Always return CFS - no detection needed
scheduler_type_t detect_best_scheduler(void)
{
    return SCHEDULER_CFS;
}

int scheduler_cascade_init(void)
{
    // Only CFS - no fallbacks, no detection
    active_scheduler_type = SCHEDULER_CFS;
    current_scheduler = cfs_scheduler_ops;
    
    if (current_scheduler.init)
    {
        current_scheduler.init();
        if (validate_scheduler_init())
        {
            return 0;
        }
    }
    
    panic("CFS initialization failed");
    return -1;
}

void scheduler_fallback_to_next(void)
{
    panic("Scheduler failed - CFS only, no fallback");
}

// No emergency scheduler - CFS only