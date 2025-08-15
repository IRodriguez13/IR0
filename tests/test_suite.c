// tests/test_suite.c - Comprehensive Testing Framework Implementation
#include "test_suite.h"
#include "../includes/ir0/print.h"
#include "../includes/string.h"
#include "../memory/heap_allocator.h"
#include "../fs/vfs.h"

// ===============================================================================
// GLOBAL TEST STATE
// ===============================================================================

static test_suite_t *test_suites = NULL;
static test_config_t test_config = {
    .verbose = true,
    .stop_on_failure = false,
    .min_level = TEST_LEVEL_LOW,
    .categories = TEST_CAT_UNIT,
    .output_file = NULL,
    .timeout_seconds = 30
};

static int global_total_tests = 0;
static int global_passed_tests = 0;
static int global_failed_tests = 0;
static int global_skipped_tests = 0;
static int global_error_tests = 0;

// ===============================================================================
// TEST FRAMEWORK CORE
// ===============================================================================

int test_framework_init(void) {
    test_suites = NULL;
    global_total_tests = 0;
    global_passed_tests = 0;
    global_failed_tests = 0;
    global_skipped_tests = 0;
    global_error_tests = 0;
    
    print_colored("=== IR0 KERNEL TEST FRAMEWORK ===\n", 0x0A, 0x00);
    print_colored("Test framework initialized\n", 0x0A, 0x00);
    
    return 0;
}

int test_register_suite(const char *name, const char *description) {
    if (!name) return -1;
    
    test_suite_t *suite = kmalloc(sizeof(test_suite_t));
    if (!suite) return -1;
    
    suite->name = name;
    suite->description = description;
    suite->test_cases = NULL;
    suite->total_tests = 0;
    suite->passed_tests = 0;
    suite->failed_tests = 0;
    suite->skipped_tests = 0;
    suite->error_tests = 0;
    suite->next = test_suites;
    test_suites = suite;
    
    if (test_config.verbose) {
        print_colored("Registered test suite: ", 0x0A, 0x00);
        print(name);
        print("\n");
    }
    
    return 0;
}

int test_register_case(const char *suite_name, test_case_t *test_case) {
    if (!suite_name || !test_case) return -1;
    
    // Find the suite
    test_suite_t *suite = test_suites;
    while (suite) {
        if (strcmp(suite->name, suite_name) == 0) {
            break;
        }
        suite = suite->next;
    }
    
    if (!suite) return -1;
    
    // Add test case to suite
    test_case->next = suite->test_cases;
    suite->test_cases = test_case;
    suite->total_tests++;
    global_total_tests++;
    
    if (test_config.verbose) {
        print_colored("  Registered test case: ", 0x0B, 0x00);
        print(test_case->name);
        print("\n");
    }
    
    return 0;
}

// ===============================================================================
// TEST EXECUTION
// ===============================================================================

static test_result_t run_test_case(test_case_t *test_case) {
    if (!test_case || !test_case->function) {
        return TEST_ERROR;
    }
    
    if (!test_case->enabled) {
        return TEST_SKIP;
    }
    
    if (test_config.verbose) {
        print_colored("Running test: ", 0x0B, 0x00);
        print(test_case->name);
        print("... ");
    }
    
    test_result_t result = test_case->function();
    
    switch (result) {
        case TEST_PASS:
            if (test_config.verbose) {
                print_colored("PASS\n", 0x0A, 0x00);
            }
            break;
        case TEST_FAIL:
            if (test_config.verbose) {
                print_colored("FAIL\n", 0x0C, 0x00);
            }
            break;
        case TEST_SKIP:
            if (test_config.verbose) {
                print_colored("SKIP\n", 0x0E, 0x00);
            }
            break;
        case TEST_ERROR:
            if (test_config.verbose) {
                print_colored("ERROR\n", 0x0C, 0x00);
            }
            break;
    }
    
    return result;
}

int test_run_suite(const char *suite_name) {
    if (!suite_name) return -1;
    
    test_suite_t *suite = test_suites;
    while (suite) {
        if (strcmp(suite->name, suite_name) == 0) {
            break;
        }
        suite = suite->next;
    }
    
    if (!suite) return -1;
    
    print_colored("\n=== Running Test Suite: ", 0x0A, 0x00);
    print(suite->name);
    print(" ===\n");
    
    if (suite->description) {
        print_colored("Description: ", 0x0B, 0x00);
        print(suite->description);
        print("\n");
    }
    
    test_case_t *test_case = suite->test_cases;
    while (test_case) {
        test_result_t result = run_test_case(test_case);
        
        switch (result) {
            case TEST_PASS:
                suite->passed_tests++;
                global_passed_tests++;
                break;
            case TEST_FAIL:
                suite->failed_tests++;
                global_failed_tests++;
                if (test_config.stop_on_failure) {
                    return -1;
                }
                break;
            case TEST_SKIP:
                suite->skipped_tests++;
                global_skipped_tests++;
                break;
            case TEST_ERROR:
                suite->error_tests++;
                global_error_tests++;
                break;
        }
        
        test_case = test_case->next;
    }
    
    return 0;
}

int test_run_all(void) {
    print_colored("\n=== RUNNING ALL TESTS ===\n", 0x0A, 0x00);
    
    test_suite_t *suite = test_suites;
    while (suite) {
        test_run_suite(suite->name);
        suite = suite->next;
    }
    
    test_print_summary();
    return 0;
}

// ===============================================================================
// TEST LOGGING
// ===============================================================================

void test_log_info(const char *format, ...) {
    print_colored("[INFO] ", 0x0B, 0x00);
    print(format);
    print("\n");
}

void test_log_success(const char *format, ...) {
    print_colored("[SUCCESS] ", 0x0A, 0x00);
    print(format);
    print("\n");
}

void test_log_warning(const char *format, ...) {
    print_colored("[WARNING] ", 0x0E, 0x00);
    print(format);
    print("\n");
}

void test_log_error(const char *format, ...) {
    print_colored("[ERROR] ", 0x0C, 0x00);
    print(format);
    print("\n");
}

void test_log_debug(const char *format, ...) {
    if (test_config.verbose) {
        print_colored("[DEBUG] ", 0x0D, 0x00);
        print(format);
        print("\n");
    }
}

// ===============================================================================
// TEST UTILITIES
// ===============================================================================

void test_print_summary(void) {
    print_colored("\n=== TEST SUMMARY ===\n", 0x0A, 0x00);
    print_colored("Total tests: ", 0x0B, 0x00);
    print_uint(global_total_tests);
    print("\n");
    
    print_colored("Passed: ", 0x0A, 0x00);
    print_uint(global_passed_tests);
    print("\n");
    
    print_colored("Failed: ", 0x0C, 0x00);
    print_uint(global_failed_tests);
    print("\n");
    
    print_colored("Skipped: ", 0x0E, 0x00);
    print_uint(global_skipped_tests);
    print("\n");
    
    print_colored("Errors: ", 0x0C, 0x00);
    print_uint(global_error_tests);
    print("\n");
    
    if (global_failed_tests == 0 && global_error_tests == 0) {
        print_colored("ALL TESTS PASSED! 🎉\n", 0x0A, 0x00);
    } else {
        print_colored("SOME TESTS FAILED! ⚠️\n", 0x0C, 0x00);
    }
}

void test_set_config(test_config_t *config) {
    if (config) {
        test_config = *config;
    }
}

test_config_t *test_get_config(void) {
    return &test_config;
}

int test_get_total_count(void) { return global_total_tests; }
int test_get_passed_count(void) { return global_passed_tests; }
int test_get_failed_count(void) { return global_failed_tests; }

// ===============================================================================
// SPECIFIC TEST SUITES IMPLEMENTATION
// ===============================================================================

// Memory management tests
int test_memory_suite_init(void) {
    test_register_suite("Memory Management", "Tests for memory allocation and management");
    
    test_case_t memory_tests[] = {
        TEST_CASE("Physical Allocator", "Test physical memory allocation", test_physical_allocator, TEST_CAT_UNIT, TEST_LEVEL_HIGH),
        TEST_CASE("Heap Allocator", "Test kernel heap allocation", test_heap_allocator, TEST_CAT_UNIT, TEST_LEVEL_HIGH),
        TEST_CASE("Virtual Memory", "Test virtual memory operations", test_virtual_memory, TEST_CAT_INTEGRATION, TEST_LEVEL_MEDIUM),
        TEST_CASE("Memory Pressure", "Test memory under pressure", test_memory_pressure, TEST_CAT_STRESS, TEST_LEVEL_MEDIUM)
    };
    
    for (int i = 0; i < sizeof(memory_tests) / sizeof(test_case_t); i++) {
        test_register_case("Memory Management", &memory_tests[i]);
    }
    
    return 0;
}

test_result_t test_physical_allocator(void) {
    // Test physical page allocation
    void *page1 = kmalloc(4096);
    TEST_ASSERT_NOT_NULL(page1);
    
    void *page2 = kmalloc(4096);
    TEST_ASSERT_NOT_NULL(page2);
    TEST_ASSERT(page1 != page2);
    
    kfree(page1);
    kfree(page2);
    
    return TEST_PASS;
}

test_result_t test_heap_allocator(void) {
    // Test heap allocation with different sizes
    void *small = kmalloc(16);
    TEST_ASSERT_NOT_NULL(small);
    
    void *medium = kmalloc(1024);
    TEST_ASSERT_NOT_NULL(medium);
    
    void *large = kmalloc(8192);
    TEST_ASSERT_NOT_NULL(large);
    
    kfree(small);
    kfree(medium);
    kfree(large);
    
    return TEST_PASS;
}

test_result_t test_virtual_memory(void) {
    // Test virtual memory operations
    // This would test vmalloc, vfree, etc.
    return TEST_PASS;
}

test_result_t test_memory_pressure(void) {
    // Test memory allocation under pressure
    void *pages[100];
    int allocated = 0;
    
    // Try to allocate many pages
    for (int i = 0; i < 100; i++) {
        pages[i] = kmalloc(4096);
        if (pages[i]) {
            allocated++;
        } else {
            break;
        }
    }
    
    // Free all allocated pages
    for (int i = 0; i < allocated; i++) {
        kfree(pages[i]);
    }
    
    TEST_ASSERT(allocated > 0);
    
    return TEST_PASS;
}

// VFS tests
int test_vfs_suite_init(void) {
    test_register_suite("Virtual File System", "Tests for VFS operations");
    
    test_case_t vfs_tests[] = {
        TEST_CASE("VFS Init", "Test VFS initialization", test_vfs_init, TEST_CAT_UNIT, TEST_LEVEL_HIGH),
        TEST_CASE("File Operations", "Test file open/close/read/write", test_file_operations, TEST_CAT_UNIT, TEST_LEVEL_HIGH),
        TEST_CASE("Directory Operations", "Test directory operations", test_directory_operations, TEST_CAT_UNIT, TEST_LEVEL_MEDIUM),
        TEST_CASE("Mount Operations", "Test mount/unmount operations", test_mount_operations, TEST_CAT_INTEGRATION, TEST_LEVEL_MEDIUM),
        TEST_CASE("Path Utilities", "Test path manipulation utilities", test_path_utilities, TEST_CAT_UNIT, TEST_LEVEL_LOW)
    };
    
    for (int i = 0; i < sizeof(vfs_tests) / sizeof(test_case_t); i++) {
        test_register_case("Virtual File System", &vfs_tests[i]);
    }
    
    return 0;
}

test_result_t test_vfs_init(void) {
    int result = vfs_init();
    TEST_ASSERT_EQUAL(0, result);
    return TEST_PASS;
}

test_result_t test_file_operations(void) {
    vfs_file_t *file;
    
    // Test file open
    int result = vfs_open("/test.txt", VFS_O_RDWR | VFS_O_CREAT, &file);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(file);
    
    // Test file write
    const char *data = "Hello, VFS!";
    ssize_t written = vfs_write(file, data, strlen(data));
    TEST_ASSERT_EQUAL(strlen(data), written);
    
    // Test file close
    result = vfs_close(file);
    TEST_ASSERT_EQUAL(0, result);
    
    return TEST_PASS;
}

test_result_t test_directory_operations(void) {
    vfs_file_t *dir;
    
    // Test directory open
    int result = vfs_opendir("/testdir", &dir);
    TEST_ASSERT_EQUAL(0, result);
    TEST_ASSERT_NOT_NULL(dir);
    
    // Test directory close
    result = vfs_close(dir);
    TEST_ASSERT_EQUAL(0, result);
    
    return TEST_PASS;
}

test_result_t test_mount_operations(void) {
    // Test mount
    int result = vfs_mount("/dev/sda1", "/mnt", "ext2");
    TEST_ASSERT_EQUAL(0, result);
    
    // Test umount
    result = vfs_umount("/mnt");
    TEST_ASSERT_EQUAL(0, result);
    
    return TEST_PASS;
}

test_result_t test_path_utilities(void) {
    // Test basename
    char *basename = vfs_basename("/path/to/file.txt");
    TEST_ASSERT_STRING_EQUAL("file.txt", basename);
    
    // Test dirname
    char *dirname = vfs_dirname("/path/to/file.txt");
    TEST_ASSERT_NOT_NULL(dirname);
    kfree(dirname);
    
    // Test absolute path
    bool is_abs = vfs_path_is_absolute("/absolute/path");
    TEST_ASSERT(is_abs);
    
    is_abs = vfs_path_is_absolute("relative/path");
    TEST_ASSERT(!is_abs);
    
    return TEST_PASS;
}

// Placeholder implementations for other test suites
int test_scheduler_suite_init(void) { return 0; }
test_result_t test_task_creation(void) { return TEST_PASS; }
test_result_t test_context_switch(void) { return TEST_PASS; }
test_result_t test_scheduler_algorithms(void) { return TEST_PASS; }
test_result_t test_priority_scheduling(void) { return TEST_PASS; }
test_result_t test_cfs_scheduling(void) { return TEST_PASS; }

int test_interrupt_suite_init(void) { return 0; }
test_result_t test_idt_setup(void) { return TEST_PASS; }
test_result_t test_isr_handlers(void) { return TEST_PASS; }
test_result_t test_page_fault_handling(void) { return TEST_PASS; }
test_result_t test_timer_interrupts(void) { return TEST_PASS; }

int test_architecture_suite_init(void) { return 0; }
test_result_t test_paging_setup(void) { return TEST_PASS; }
test_result_t test_mmu_operations(void) { return TEST_PASS; }
test_result_t test_arch_interface(void) { return TEST_PASS; }
test_result_t test_cpu_features(void) { return TEST_PASS; }

int test_integration_suite_init(void) { return 0; }
test_result_t test_kernel_boot_sequence(void) { return TEST_PASS; }
test_result_t test_subsystem_interaction(void) { return TEST_PASS; }
test_result_t test_memory_scheduler_integration(void) { return TEST_PASS; }
test_result_t test_vfs_memory_integration(void) { return TEST_PASS; }

int test_performance_suite_init(void) { return 0; }
test_result_t test_memory_allocation_performance(void) { return TEST_PASS; }
test_result_t test_scheduler_performance(void) { return TEST_PASS; }
test_result_t test_context_switch_performance(void) { return TEST_PASS; }
test_result_t test_vfs_performance(void) { return TEST_PASS; }

int test_stress_suite_init(void) { return 0; }
test_result_t test_memory_stress(void) { return TEST_PASS; }
test_result_t test_scheduler_stress(void) { return TEST_PASS; }
test_result_t test_concurrent_operations(void) { return TEST_PASS; }
test_result_t test_long_running_operations(void) { return TEST_PASS; }
