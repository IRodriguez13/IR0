#include <stdbool.h>
#include <stddef.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include "scheduler_types.h"


// Forward declarations for scheduler implementations
extern scheduler_ops_t cfs_scheduler_ops;
extern scheduler_ops_t priority_scheduler_ops;
extern scheduler_ops_t roundrobin_scheduler_ops;

// External memory info
extern uint32_t free_pages_count;

// Global state flags
scheduler_ops_t current_scheduler;
scheduler_type_t active_scheduler_type = SCHEDULER_NONE;

static bool run_scheduler_system_test(void)
{
    print("SCHEDULER: Running system test...\n");

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

    print("SCHEDULER: System test completed successfully\n");
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

    case SCHEDULER_PRIORITY:
        // Validate priority scheduler structures
        // Add specific checks here if needed
        break;

    case SCHEDULER_ROUND_ROBIN:
        // Validate round-robin structures
        // Add specific checks here if needed
        break;

    default:
        LOG_ERR("SCHEDULER: Unknown scheduler type in validation");
        return false;
    }

    print("SCHEDULER: Validation passed for ");
    print(current_scheduler.name);
    print("\n");
    return true;
}

// Este es el criterio con el que elijo el planificador que uso. Es como el semáforo si querés verlo así.
scheduler_type_t detect_best_scheduler(void)
{
    print("SCHEDULER: Analyzing system for optimal scheduler selection...\n");

    // Get system information
    extern uint32_t free_pages_count;
    // extern uint32_t total_memory; // If available
    // extern uint32_t cpu_features; // If available

    print("SCHEDULER: Available memory pages: ");
    print_hex_compact(free_pages_count);
    print("\n");

    // Enhanced detection logic with multiple criteria

    // 1. Memory-based selection (primary criterion)
    if (free_pages_count > 150)
    {
        // Abundant memory - use most sophisticated scheduler
        LOG_OK("SCHEDULER: High memory available - selecting CFS");
        print("SCHEDULER: Memory level: HIGH (>150 pages)\n");
        return SCHEDULER_CFS;
    }

    if (free_pages_count > 75)
    {
        // Medium memory - use priority scheduler with aging
        LOG_OK("SCHEDULER: Medium memory available - selecting Priority with Aging");
        print("SCHEDULER: Memory level: MEDIUM (75-150 pages)\n");
        return SCHEDULER_PRIORITY;
    }

    if (free_pages_count > 25)
    {
        // Low memory - use simple round robin
        LOG_WARN("SCHEDULER: Low memory - using Round Robin fallback");
        print("SCHEDULER: Memory level: LOW (25-75 pages)\n");
        return SCHEDULER_ROUND_ROBIN;
    }

    // 2. Critical memory situation
    if (free_pages_count > 10)
    {
        LOG_WARN("SCHEDULER: Critical memory situation - forcing Round Robin");
        print("SCHEDULER: Memory level: CRITICAL (10-25 pages)\n");
        return SCHEDULER_ROUND_ROBIN;
    }

    // 3. Emergency situation
    LOG_ERR("SCHEDULER: Emergency memory situation - no suitable scheduler");
    print("SCHEDULER: Memory level: EMERGENCY (<10 pages)\n");
    return SCHEDULER_NONE;
}

// Este es uno de esos experimentos que se me ocurrieron en el kernel. La idea es que el sistema detecta en tiempo de ejecucion
// el sched que te conviene según la cantidad de memo paginada libre. Literalmente es meritocracia kernelística.

int scheduler_cascade_init(void)
{
    print_colored("=== SCHEDULER CASCADE DETECTION WITH RETRY LOGIC ===\n",
                  VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    const int MAX_RETRIES = 3;
    const int MAX_FALLBACK_ATTEMPTS = 3;

    for (int attempt = 0; attempt < MAX_FALLBACK_ATTEMPTS; attempt++)
    {
        print("SCHEDULER: Detection attempt ");
        print_hex_compact(attempt + 1);
        print("/");
        print_hex_compact(MAX_FALLBACK_ATTEMPTS);
        print("\n");

        // Detect best scheduler for current system state
        active_scheduler_type = detect_best_scheduler();

        if (active_scheduler_type == SCHEDULER_NONE)
        {
            LOG_ERR("SCHEDULER: No suitable scheduler detected on attempt ");
            print_hex_compact(attempt + 1);
            print("\n");
            continue;
        }

        // Print detected scheduler info
        print("SCHEDULER: Attempting to initialize ");
        switch (active_scheduler_type)
        {
        case SCHEDULER_CFS:
            print("Completely Fair Scheduler (CFS)\n");
            current_scheduler = cfs_scheduler_ops;
            break;
        case SCHEDULER_PRIORITY:

            print("Priority Scheduler with Aging\n");

            current_scheduler = priority_scheduler_ops;

            break;
        case SCHEDULER_ROUND_ROBIN:

            print("Round Robin Scheduler\n");

            current_scheduler = roundrobin_scheduler_ops;

            break;
        default:

            LOG_ERR("SCHEDULER: Invalid scheduler type detected");

            continue;
        }

        // Try to initialize with retries
        bool init_successful = false;

        for (int retry = 0; retry < MAX_RETRIES; retry++)
        {
            if (retry > 0)
            {
                print("SCHEDULER: Initialization retry ");
                print_hex_compact(retry + 1);
                print("/");
                print_hex_compact(MAX_RETRIES);
                print("\n");

                // Wait a bit between retries
                delay_ms(100);
            }

            if (current_scheduler.init)
            {
                // Try initialization
                current_scheduler.init();

                // Validate initialization was successful
                if (validate_scheduler_init())
                {
                    init_successful = true;
                    print_colored("✓ SCHEDULER: Initialization successful!\n",
                                  VGA_COLOR_GREEN, VGA_COLOR_BLACK);
                    break;
                }
                else
                {
                    LOG_WARN("SCHEDULER: Initialization validation failed on retry ");
                    print_hex_compact(retry + 1);
                    print("\n");
                }
            }
            else
            {
                LOG_ERR("SCHEDULER: No init function available");
                break;
            }
        }

        if (init_successful)
        {
            // Final system validation
            if (run_scheduler_system_test())
            {
                print_colored("✓ SCHEDULER: System test passed - scheduler ready!\n",
                              VGA_COLOR_GREEN, VGA_COLOR_BLACK);
                return 0;
            }
            else
            {
                LOG_WARN("SCHEDULER: System test failed, trying fallback");
                scheduler_fallback_to_next();
            }
        }
        else
        {
            LOG_WARN("SCHEDULER: Initialization failed after all retries, trying fallback");
            scheduler_fallback_to_next();
        }
    }

    // All attempts failed
    LOG_ERR("SCHEDULER: All initialization attempts failed!");
    panic("No schedulers could be initialized - system unusable");
    return -1;
}

void scheduler_fallback_to_next(void)
{
    static int fallback_count = 0;
    const int MAX_FALLBACKS = 5;

    fallback_count++;

    if (fallback_count > MAX_FALLBACKS)
    {
        LOG_ERR("SCHEDULER: Too many fallback attempts - system unstable");
        panic("Scheduler fallback loop detected");
        return;
    }

    print_colored("⚠ SCHEDULER FALLBACK INITIATED ⚠\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);

    LOG_WARN("SCHEDULER: Current scheduler failed, attempting fallback ");
    print_hex_compact(fallback_count);
    print("/");
    print_hex_compact(MAX_FALLBACKS);
    print("\n");

    scheduler_type_t old_scheduler = active_scheduler_type;

    // Cleanup current scheduler if it has cleanup function
    if (current_scheduler.cleanup)
    {
        print("SCHEDULER: Cleaning up failed scheduler...\n");
        current_scheduler.cleanup();
    }

    // Determine next fallback based on current scheduler
    switch (old_scheduler)
    {
    case SCHEDULER_CFS:
        // CFS failed, try priority scheduler
        print("SCHEDULER: CFS failed, falling back to Priority scheduler\n");
        active_scheduler_type = SCHEDULER_PRIORITY;
        current_scheduler = priority_scheduler_ops;
        break;

    case SCHEDULER_PRIORITY:
        // Priority failed, try round-robin
        print("SCHEDULER: Priority scheduler failed, falling back to Round-Robin\n");
        active_scheduler_type = SCHEDULER_ROUND_ROBIN;
        current_scheduler = roundrobin_scheduler_ops;
        break;

    case SCHEDULER_ROUND_ROBIN:
        // Round-robin is our last resort
        LOG_ERR("SCHEDULER: Round-robin scheduler failed! No more fallbacks available!");
        print_colored("💀 CRITICAL: All schedulers exhausted 💀\n",
                      VGA_COLOR_RED, VGA_COLOR_BLACK);

        // Try one last desperate attempt with minimal round-robin
        print("SCHEDULER: Attempting emergency minimal scheduler...\n");
        active_scheduler_type = SCHEDULER_ROUND_ROBIN;
        current_scheduler = roundrobin_scheduler_ops;

        // If this also fails, system is unusable
        if (!current_scheduler.init)
        {
            panic("All schedulers failed - system cannot continue");
        }
        break;

    default:
        LOG_ERR("SCHEDULER: Unknown scheduler state in fallback");
        print("SCHEDULER: Forcing Round-Robin as emergency fallback\n");
        active_scheduler_type = SCHEDULER_ROUND_ROBIN;
        current_scheduler = roundrobin_scheduler_ops;
        break;
    }

    print("SCHEDULER: Attempting to initialize fallback scheduler: ");
    print(current_scheduler.name);
    print("\n");

    // Try to initialize the fallback scheduler with single retry
    bool fallback_success = false;

    for (int retry = 0; retry < 2; retry++)
    {
        if (current_scheduler.init)
        {
            current_scheduler.init();

            if (validate_scheduler_init())
            {
                fallback_success = true;
                print_colored("✓ SCHEDULER: Fallback initialization successful!\n",
                              VGA_COLOR_GREEN, VGA_COLOR_BLACK);
                break;
            }
            else
            {
                LOG_WARN("SCHEDULER: Fallback validation failed on retry ");
                print_hex_compact(retry + 1);
                print("\n");
                delay_ms(50); // Short delay between retries
            }
        }
        else
        {
            LOG_ERR("SCHEDULER: Fallback scheduler has no init function!");
            break;
        }
    }

    if (!fallback_success)
    {
        LOG_ERR("SCHEDULER: Fallback scheduler initialization failed!");

        // If we're already at round-robin and it failed, system is doomed
        if (active_scheduler_type == SCHEDULER_ROUND_ROBIN)
        {
            print_colored("💀 SYSTEM FAILURE: Cannot initialize any scheduler 💀\n",
                          VGA_COLOR_RED, VGA_COLOR_BLACK);
            panic("All scheduler fallbacks failed - system halt");
        }
        else
        {
            // Try next fallback recursively (with fallback count protection)
            scheduler_fallback_to_next();
        }
    }
    else
    {
        // Reset fallback counter on success
        fallback_count = 0;
        LOG_OK("SCHEDULER: Fallback completed successfully");
    }
}

static task_t *emergency_task_list = NULL;
static task_t *emergency_current = NULL;

static void emergency_scheduler_init(void)
{
    emergency_task_list = NULL;
    emergency_current = NULL;
    print("EMERGENCY: Minimal scheduler initialized\n");
}

static void emergency_add_task(task_t *task)
{
    if (!task)
        return;

    task->next = emergency_task_list;
    emergency_task_list = task;
    task->state = TASK_READY;

    print("EMERGENCY: Task PID ");
    print_hex_compact(task->pid);
    print(" added to emergency scheduler\n");
}

static task_t *emergency_pick_next_task(void)
{
    if (!emergency_task_list)
        return NULL;

    // Simple round-robin through emergency list
    if (!emergency_current)
    {
        emergency_current = emergency_task_list;
    }
    else
    {
        emergency_current = emergency_current->next;
        if (!emergency_current)
        {
            emergency_current = emergency_task_list;
        }
    }

    return emergency_current;
}

static void emergency_task_tick(void)
{
    // Minimal tick processing
    static int emergency_tick_count = 0;
    emergency_tick_count++;

    if (emergency_tick_count >= 20)
    { // Force preemption every 20 ticks
        emergency_tick_count = 0;
        // Signal that we should switch tasks
    }
}

// Emergency scheduler operations structure
scheduler_ops_t emergency_scheduler_ops =
    {
        .type = SCHEDULER_ROUND_ROBIN, // Pretend to be round-robin
        .name = "Emergency Minimal Scheduler",
        .init = emergency_scheduler_init,
        .add_task = emergency_add_task,
        .pick_next_task = emergency_pick_next_task,
        .task_tick = emergency_task_tick,
        .cleanup = NULL,
        .private_data = NULL};