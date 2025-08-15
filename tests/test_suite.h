// tests/test_suite.h - Comprehensive Testing Framework
#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ===============================================================================
// TEST FRAMEWORK TYPES
// ===============================================================================

// Test result codes
typedef enum {
    TEST_PASS = 0,
    TEST_FAIL = 1,
    TEST_SKIP = 2,
    TEST_ERROR = 3
} test_result_t;

// Test severity levels
typedef enum {
    TEST_LEVEL_LOW = 0,
    TEST_LEVEL_MEDIUM = 1,
    TEST_LEVEL_HIGH = 2,
    TEST_LEVEL_CRITICAL = 3
} test_level_t;

// Test categories
typedef enum {
    TEST_CAT_UNIT = 0,
    TEST_CAT_INTEGRATION = 1,
    TEST_CAT_SYSTEM = 2,
    TEST_CAT_PERFORMANCE = 3,
    TEST_CAT_STRESS = 4
} test_category_t;

// Test function signature
typedef test_result_t (*test_function_t)(void);

// Test case structure
typedef struct test_case {
    const char *name;
    const char *description;
    test_function_t function;
    test_category_t category;
    test_level_t level;
    bool enabled;
    struct test_case *next;
} test_case_t;

// Test suite structure
typedef struct test_suite {
    const char *name;
    const char *description;
    test_case_t *test_cases;
    int total_tests;
    int passed_tests;
    int failed_tests;
    int skipped_tests;
    int error_tests;
    struct test_suite *next;
} test_suite_t;

// Test runner configuration
typedef struct test_config {
    bool verbose;
    bool stop_on_failure;
    test_level_t min_level;
    test_category_t categories;
    const char *output_file;
    int timeout_seconds;
} test_config_t;

// ===============================================================================
// TEST MACROS AND UTILITIES
// ===============================================================================

// Test assertion macros
#define TEST_ASSERT(condition) \
    do { \
        if (!(condition)) { \
            test_log_error("Assertion failed: " #condition); \
            return TEST_FAIL; \
        } \
    } while(0)

#define TEST_ASSERT_EQUAL(expected, actual) \
    do { \
        if ((expected) != (actual)) { \
            test_log_error("Assertion failed: expected %d, got %d", (expected), (actual)); \
            return TEST_FAIL; \
        } \
    } while(0)

#define TEST_ASSERT_NOT_NULL(ptr) \
    do { \
        if ((ptr) == NULL) { \
            test_log_error("Assertion failed: pointer is NULL"); \
            return TEST_FAIL; \
        } \
    } while(0)

#define TEST_ASSERT_NULL(ptr) \
    do { \
        if ((ptr) != NULL) { \
            test_log_error("Assertion failed: pointer is not NULL"); \
            return TEST_FAIL; \
        } \
    } while(0)

#define TEST_ASSERT_STRING_EQUAL(expected, actual) \
    do { \
        if (strcmp((expected), (actual)) != 0) { \
            test_log_error("String assertion failed: expected '%s', got '%s'", (expected), (actual)); \
            return TEST_FAIL; \
        } \
    } while(0)

// Test case registration macro
#define TEST_CASE(name, desc, func, cat, level) \
    { name, desc, func, cat, level, true, NULL }

// ===============================================================================
// TEST FRAMEWORK INTERFACE
// ===============================================================================

// Initialize test framework
int test_framework_init(void);

// Register test suite
int test_register_suite(const char *name, const char *description);

// Register test case
int test_register_case(const char *suite_name, test_case_t *test_case);

// Run all tests
int test_run_all(void);

// Run specific test suite
int test_run_suite(const char *suite_name);

// Run specific test case
int test_run_case(const char *suite_name, const char *case_name);

// Test configuration
void test_set_config(test_config_t *config);
test_config_t *test_get_config(void);

// Test logging
void test_log_info(const char *format, ...);
void test_log_success(const char *format, ...);
void test_log_warning(const char *format, ...);
void test_log_error(const char *format, ...);
void test_log_debug(const char *format, ...);

// Test utilities
void test_print_summary(void);
void test_print_report(void);
int test_get_total_count(void);
int test_get_passed_count(void);
int test_get_failed_count(void);

// ===============================================================================
// SPECIFIC TEST SUITES
// ===============================================================================

// Memory management tests
int test_memory_suite_init(void);
test_result_t test_physical_allocator(void);
test_result_t test_heap_allocator(void);
test_result_t test_virtual_memory(void);
test_result_t test_memory_pressure(void);

// Scheduler tests
int test_scheduler_suite_init(void);
test_result_t test_task_creation(void);
test_result_t test_context_switch(void);
test_result_t test_scheduler_algorithms(void);
test_result_t test_priority_scheduling(void);
test_result_t test_cfs_scheduling(void);

// VFS tests
int test_vfs_suite_init(void);
test_result_t test_vfs_init(void);
test_result_t test_file_operations(void);
test_result_t test_directory_operations(void);
test_result_t test_mount_operations(void);
test_result_t test_path_utilities(void);

// Interrupt tests
int test_interrupt_suite_init(void);
test_result_t test_idt_setup(void);
test_result_t test_isr_handlers(void);
test_result_t test_page_fault_handling(void);
test_result_t test_timer_interrupts(void);

// Architecture tests
int test_architecture_suite_init(void);
test_result_t test_paging_setup(void);
test_result_t test_mmu_operations(void);
test_result_t test_arch_interface(void);
test_result_t test_cpu_features(void);

// Integration tests
int test_integration_suite_init(void);
test_result_t test_kernel_boot_sequence(void);
test_result_t test_subsystem_interaction(void);
test_result_t test_memory_scheduler_integration(void);
test_result_t test_vfs_memory_integration(void);

// Performance tests
int test_performance_suite_init(void);
test_result_t test_memory_allocation_performance(void);
test_result_t test_scheduler_performance(void);
test_result_t test_context_switch_performance(void);
test_result_t test_vfs_performance(void);

// Stress tests
int test_stress_suite_init(void);
test_result_t test_memory_stress(void);
test_result_t test_scheduler_stress(void);
test_result_t test_concurrent_operations(void);
test_result_t test_long_running_operations(void);
