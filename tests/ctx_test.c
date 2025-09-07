// ===============================================================================
// CONTEXT SWITCH TEST SUITE - Comprehensive Testing Framework
// ===============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <ir0/print.h>
#include <ir0/panic/panic.h>
#include "../kernel/scheduler/scheduler_types.h"
#include "../kernel/scheduler/task.h"

// ===============================================================================
// TEST CONFIGURATION AND CONSTANTS
// ===============================================================================

#define MAX_TEST_TASKS 10
#define TEST_STACK_SIZE (8 * 1024)
#define TEST_MAGIC_NUMBER 0xDEADBEEF
#define TEST_CANARY_VALUE 0xCAFEBABE
#define CONTEXT_SWITCH_TEST_ITERATIONS 100

// Test result structure
typedef struct
{
    int total_tests;
    int passed_tests;
    int failed_tests;
    int critical_failures;
    char last_error[256];
} test_results_t;

// Test task structure
typedef struct test_task
{
    task_t task;
    uint32_t task_id;
    uint32_t execution_count;
    uint32_t expected_value;
    uint32_t actual_value;
    uint64_t start_time;
    uint64_t end_time;
    bool completed;
    uint32_t stack_canary_top;
    uint32_t stack_canary_bottom;
    struct test_task *next;
} test_task_t;

// Global test state
static test_results_t test_results = {0};
static test_task_t *test_tasks_list = NULL;
static volatile bool test_in_progress = false;
static volatile uint32_t current_test_phase = 0;

// ===============================================================================
// UTILITY FUNCTIONS
// ===============================================================================

static void test_log(const char *message)
{
    print("[CTX-TEST] ");
    print(message);
    print("\n");
}

static void test_error(const char *error)
{
    print("[CTX-TEST ERROR] ");
    print(error);
    print("\n");
    strncpy(test_results.last_error, error, sizeof(test_results.last_error) - 1);
    test_results.failed_tests++;
}

static void test_pass(const char *message)
{
    print("[CTX-TEST PASS] ");
    print(message);
    print("\n");
    test_results.passed_tests++;
}

static uint64_t get_current_ticks(void)
{
    // Get current system ticks - you'll need to implement this based on your timer
    extern uint32_t scheduler_state_tick_count; // From your scheduler
    return (uint64_t)scheduler_state_tick_count;
}

static bool validate_stack_integrity(test_task_t *test_task)
{
    // Check stack
    if (test_task->stack_canary_top != TEST_CANARY_VALUE)
    {
        test_error("Stack overflow detected (top canary corrupted)");
        return false;
    }

    if (test_task->stack_canary_bottom != TEST_CANARY_VALUE)
    {
        test_error("Stack underflow detected (bottom canary corrupted)");
        return false;
    }

    return true;
}

// ===============================================================================
// TEST TASK FUNCTIONS
// ===============================================================================

// Simple test task that increments a counter
void test_task_counter(void *arg)
{
    test_task_t *test_task = (test_task_t *)arg;

    test_task->start_time = get_current_ticks();

    print("[TEST-TASK-");
    print_uint32(test_task->task_id);
    print("] Started execution\n");

    // Perform some work and validate registers
    volatile uint32_t local_counter = 0;
    volatile uint32_t expected = test_task->expected_value;

    for (int i = 0; i < 1000; i++)
    {
        local_counter += expected;

        // Yield periodically to test context switching
        if (i % 100 == 0)
        {
            print("[TEST-TASK-");
            print_uint32(test_task->task_id);
            print("] Yielding at iteration ");
            print_uint32(i);
            print("\n");

            // Force context switch
            extern void scheduler_yield(void);
            scheduler_yield();
        }
    }

    test_task->actual_value = local_counter;
    test_task->execution_count++;
    test_task->end_time = get_current_ticks();
    test_task->completed = true;

    print("[TEST-TASK-");
    print_uint32(test_task->task_id);
    print("] Completed execution. Counter: ");
    print_uint32(local_counter);
    print("\n");
}

// Test task that validates register preservation
void test_task_register_validation(void *arg)
{
    test_task_t *test_task = (test_task_t *)arg;

    test_task->start_time = get_current_ticks();

    // Set up test values in registers (using inline assembly)
    uint64_t test_rax = 0x1111111111111111ULL;
    uint64_t test_rbx = 0x2222222222222222ULL;
    uint64_t test_rcx = 0x3333333333333333ULL;
    uint64_t test_rdx = 0x4444444444444444ULL;

    // Load test values into registers
    __asm__ volatile(
        "mov %0, %%rax\n"
        "mov %1, %%rbx\n"
        "mov %2, %%rcx\n"
        "mov %3, %%rdx\n"
        :
        : "r"(test_rax), "r"(test_rbx), "r"(test_rcx), "r"(test_rdx)
        : "rax", "rbx", "rcx", "rdx");

    print("[REG-TEST-");
    print_uint32(test_task->task_id);
    print("] Registers loaded, yielding...\n");

    // Force context switch
    extern void scheduler_yield(void);
    scheduler_yield();

    // Validate registers after context switch
    uint64_t actual_rax, actual_rbx, actual_rcx, actual_rdx;

    __asm__ volatile(
        "mov %%rax, %0\n"
        "mov %%rbx, %1\n"
        "mov %%rcx, %2\n"
        "mov %%rdx, %3\n"
        : "=r"(actual_rax), "=r"(actual_rbx), "=r"(actual_rcx), "=r"(actual_rdx)
        :
        : "memory");

    bool registers_ok = true;

    if (actual_rax != test_rax)
    {
        test_error("RAX register corruption detected");
        registers_ok = false;
    }

    if (actual_rbx != test_rbx)
    {
        test_error("RBX register corruption detected");
        registers_ok = false;
    }

    if (actual_rcx != test_rcx)
    {
        test_error("RCX register corruption detected");
        registers_ok = false;
    }

    if (actual_rdx != test_rdx)
    {
        test_error("RDX register corruption detected");
        registers_ok = false;
    }

    if (registers_ok)
    {
        test_pass("All registers preserved correctly");
    }

    test_task->completed = true;
    test_task->end_time = get_current_ticks();
}

// Test task that validates stack preservation
void test_task_stack_validation(void *arg)
{
    test_task_t *test_task = (test_task_t *)arg;

    test_task->start_time = get_current_ticks();

    // Create stack-based data
    volatile uint32_t stack_data[256];
    for (int i = 0; i < 256; i++)
    {
        stack_data[i] = TEST_MAGIC_NUMBER + i;
    }

    print("[STACK-TEST-");
    print_uint32(test_task->task_id);
    print("] Stack data initialized, yielding...\n");

    // Force context switch
    extern void scheduler_yield(void);
    scheduler_yield();

    // Validate stack data
    bool stack_ok = true;
    for (int i = 0; i < 256; i++)
    {
        if (stack_data[i] != TEST_MAGIC_NUMBER + i)
        {
            test_error("Stack corruption detected");
            stack_ok = false;
            break;
        }
    }

    if (stack_ok)
    {
        test_pass("Stack data preserved correctly");
    }

    test_task->completed = true;
    test_task->end_time = get_current_ticks();
}

// ===============================================================================
// TEST MANAGEMENT FUNCTIONS
// ===============================================================================

static test_task_t *create_test_task(uint32_t task_id, void (*entry)(void *), uint32_t expected_value)
{
    test_task_t *test_task = malloc(sizeof(test_task_t));
    if (!test_task)
    {
        test_error("Failed to allocate test task");
        return NULL;
    }

    memset(test_task, 0, sizeof(test_task_t));

    // Initialize test task structure
    test_task->task_id = task_id;
    test_task->expected_value = expected_value;
    test_task->stack_canary_top = TEST_CANARY_VALUE;
    test_task->stack_canary_bottom = TEST_CANARY_VALUE;

    // Create actual task
    extern task_t *create_task(void (*entry)(void *), void *arg, uint8_t priority, int8_t nice);
    task_t *real_task = create_task(entry, test_task, 1, 0);

    if (!real_task)
    {
        free(test_task);
        test_error("Failed to create real task");
        return NULL;
    }

    // Copy task structure
    memcpy(&test_task->task, real_task, sizeof(task_t));

    // Add to test tasks list
    test_task->next = test_tasks_list;
    test_tasks_list = test_task;

    return test_task;
}

static void cleanup_test_tasks(void)
{
    test_task_t *current = test_tasks_list;

    while (current)
    {
        test_task_t *next = current->next;

        // Validate stack integrity before cleanup
        if (!validate_stack_integrity(current))
        {
            test_results.critical_failures++;
        }

        free(current);
        current = next;
    }

    test_tasks_list = NULL;
}

// ===============================================================================
// MAIN TEST FUNCTIONS
// ===============================================================================

int test_basic_context_switch(void)
{
    test_log("Starting basic context switch test...");
    test_results.total_tests++;

    // Create two simple counter tasks
    test_task_t *task1 = create_test_task(1, test_task_counter, 10);
    test_task_t *task2 = create_test_task(2, test_task_counter, 20);

    if (!task1 || !task2)
    {
        test_error("Failed to create test tasks");
        return -1;
    }

    // Add tasks to scheduler
    extern void add_task(task_t * task);
    add_task(&task1->task);
    add_task(&task2->task);

    // Wait for tasks to complete
    uint64_t start_time = get_current_ticks();
    uint64_t timeout = start_time + 10000; // 10 second timeout

    while ((!task1->completed || !task2->completed) && get_current_ticks() < timeout)
    {
        // Let scheduler run
        extern void scheduler_tick(void);
        scheduler_tick();

        // Small delay
        for (volatile int i = 0; i < 1000; i++)
            ;
    }

    // Check results
    bool test_passed = true;

    if (!task1->completed)
    {
        test_error("Task 1 did not complete");
        test_passed = false;
    }

    if (!task2->completed)
    {
        test_error("Task 2 did not complete");
        test_passed = false;
    }

    if (task1->completed && task1->actual_value != 10000)
    {
        test_error("Task 1 produced incorrect result");
        test_passed = false;
    }

    if (task2->completed && task2->actual_value != 20000)
    {
        test_error("Task 2 produced incorrect result");
        test_passed = false;
    }

    if (test_passed)
    {
        test_pass("Basic context switch test");
        return 0;
    }
    else
    {
        return -1;
    }
}

int test_register_preservation(void)
{
    test_log("Starting register preservation test...");
    test_results.total_tests++;

    // Create register validation task
    test_task_t *reg_task = create_test_task(100, test_task_register_validation, 0);

    if (!reg_task)
    {
        test_error("Failed to create register test task");
        return -1;
    }

    // Add task to scheduler
    extern void add_task(task_t * task);
    add_task(&reg_task->task);

    // Wait for task to complete
    uint64_t start_time = get_current_ticks();
    uint64_t timeout = start_time + 5000;

    while (!reg_task->completed && get_current_ticks() < timeout)
    {
        extern void scheduler_tick(void);
        scheduler_tick();

        for (volatile int i = 0; i < 1000; i++)
            ;
    }

    if (!reg_task->completed)
    {
        test_error("Register test task did not complete");
        return -1;
    }

    return 0;
}

int test_stack_preservation(void)
{
    test_log("Starting stack preservation test...");
    test_results.total_tests++;

    // Create stack validation task
    test_task_t *stack_task = create_test_task(200, test_task_stack_validation, 0);

    if (!stack_task)
    {
        test_error("Failed to create stack test task");
        return -1;
    }

    // Add task to scheduler
    extern void add_task(task_t * task);
    add_task(&stack_task->task);

    // Wait for task to complete
    uint64_t start_time = get_current_ticks();
    uint64_t timeout = start_time + 5000;

    while (!stack_task->completed && get_current_ticks() < timeout)
    {
        extern void scheduler_tick(void);
        scheduler_tick();

        for (volatile int i = 0; i < 1000; i++)
            ;
    }

    if (!stack_task->completed)
    {
        test_error("Stack test task did not complete");
        return -1;
    }

    return 0;
}

int test_multiple_context_switches(void)
{
    test_log("Starting multiple context switches test...");
    test_results.total_tests++;

    const int NUM_TASKS = 5;
    test_task_t *tasks[NUM_TASKS];

    // Create multiple tasks
    for (int i = 0; i < NUM_TASKS; i++)
    {
        tasks[i] = create_test_task(300 + i, test_task_counter, (i + 1) * 5);
        if (!tasks[i])
        {
            test_error("Failed to create multiple test tasks");
            return -1;
        }

        extern void add_task(task_t * task);
        add_task(&tasks[i]->task);
    }

    // Wait for all tasks to complete
    uint64_t start_time = get_current_ticks();
    uint64_t timeout = start_time + 15000; // Longer timeout for multiple tasks

    bool all_completed = false;
    while (!all_completed && get_current_ticks() < timeout)
    {
        all_completed = true;

        for (int i = 0; i < NUM_TASKS; i++)
        {
            if (!tasks[i]->completed)
            {
                all_completed = false;
                break;
            }
        }

        extern void scheduler_tick(void);
        scheduler_tick();

        for (volatile int i = 0; i < 1000; i++)
            ;
    }

    // Validate results
    bool test_passed = true;
    for (int i = 0; i < NUM_TASKS; i++)
    {
        if (!tasks[i]->completed)
        {
            print("Task ");
            print_uint32(i);
            print(" did not complete\n");
            test_passed = false;
        }
    }

    if (test_passed)
    {
        test_pass("Multiple context switches test");
        return 0;
    }
    else
    {
        test_error("Multiple context switches test failed");
        return -1;
    }
}

// ===============================================================================
// MAIN TEST RUNNER
// ===============================================================================

int run_context_switch_tests(void)
{
    print_colored("╔══════════════════════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                     CONTEXT SWITCH TEST SUITE                               ║\n", VGA_COLOR_WHITE, VGA_COLOR_BLACK);
    print_colored("║                     Testing Assembly Implementation                          ║\n", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Initialize test state
    memset(&test_results, 0, sizeof(test_results));
    test_in_progress = true;

    // Run individual tests
    test_basic_context_switch();
    test_register_preservation();
    test_stack_preservation();
    test_multiple_context_switches();

    // Cleanup
    cleanup_test_tasks();
    test_in_progress = false;

    // Print results
    print_colored("\n=== CONTEXT SWITCH TEST RESULTS ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print("Total Tests: ");
    print_uint32(test_results.total_tests);
    print("\n");

    print("Passed: ");
    print_uint32(test_results.passed_tests);
    print("\n");

    print("Failed: ");
    print_uint32(test_results.failed_tests);
    print("\n");

    print("Critical Failures: ");
    print_uint32(test_results.critical_failures);
    print("\n");

    if (test_results.failed_tests == 0 && test_results.critical_failures == 0)
    {
        print_colored("✅ ALL CONTEXT SWITCH TESTS PASSED!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        return 0;
    }
    else
    {
        print_colored("❌ SOME TESTS FAILED!\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        if (strlen(test_results.last_error) > 0)
        {
            print("Last Error: ");
            print(test_results.last_error);
            print("\n");
        }
        return -1;
    }
}

// ===============================================================================
// QUICK TEST FUNCTION FOR SHELL COMMAND
// ===============================================================================

void test_context_switch_quick(void)
{
    print("Running quick context switch test...\n");

    if (run_context_switch_tests() == 0)
    {
        print("Quick test PASSED ✅\n");
    }
    else
    {
        print("Quick test FAILED ❌\n");
    }
}