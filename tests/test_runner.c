#include "test_suite.h"
#include "../includes/ir0/print.h"
#include "../includes/string.h"

// ===============================================================================
// SIMPLE TEST RUNNER FOR BUMP ALLOCATOR STRESS TESTS
// ===============================================================================

int run_bump_allocator_stress_tests(void) {
    print_colored("=== BUMP ALLOCATOR STRESS TESTS ===\n", 0x0A, 0x00);
    
    // Initialize test framework
    if (test_framework_init() != 0) {
        print_colored("Failed to initialize test framework\n", 0x0C, 0x00);
        return -1;
    }
    
    // Initialize bump allocator stress test suite
    if (test_bump_allocator_stress_suite_init() != 0) {
        print_colored("Failed to initialize bump allocator stress tests\n", 0x0C, 0x00);
        return -1;
    }
    
    // Run all tests
    test_run_all();
    
    // Print final summary
    test_print_summary();
    
    return 0;
}

// Individual test runners for specific stress tests
int run_bump_allocator_basic_stress(void) {
    print_colored("=== RUNNING BUMP ALLOCATOR BASIC STRESS TEST ===\n", 0x0A, 0x00);
    
    if (test_framework_init() != 0) {
        print_colored("Failed to initialize test framework\n", 0x0C, 0x00);
        return -1;
    }
    
    test_result_t result = test_bump_allocator_basic_stress();
    
    if (result == TEST_PASS) {
        print_colored("Basic stress test PASSED\n", 0x0A, 0x00);
        return 0;
    } else {
        print_colored("Basic stress test FAILED\n", 0x0C, 0x00);
        return -1;
    }
}

int run_bump_allocator_boundary_stress(void) {
    print_colored("=== RUNNING BUMP ALLOCATOR BOUNDARY STRESS TEST ===\n", 0x0A, 0x00);
    
    if (test_framework_init() != 0) {
        print_colored("Failed to initialize test framework\n", 0x0C, 0x00);
        return -1;
    }
    
    test_result_t result = test_bump_allocator_boundary_stress();
    
    if (result == TEST_PASS) {
        print_colored("Boundary stress test PASSED\n", 0x0A, 0x00);
        return 0;
    } else {
        print_colored("Boundary stress test FAILED\n", 0x0C, 0x00);
        return -1;
    }
}

int run_bump_allocator_corruption_stress(void) {
    print_colored("=== RUNNING BUMP ALLOCATOR CORRUPTION STRESS TEST ===\n", 0x0A, 0x00);
    
    if (test_framework_init() != 0) {
        print_colored("Failed to initialize test framework\n", 0x0C, 0x00);
        return -1;
    }
    
    test_result_t result = test_bump_allocator_corruption_stress();
    
    if (result == TEST_PASS) {
        print_colored("Corruption stress test PASSED\n", 0x0A, 0x00);
        return 0;
    } else {
        print_colored("Corruption stress test FAILED\n", 0x0C, 0x00);
        return -1;
    }
}

// Quick stress test that runs a subset of the most important tests
int run_bump_allocator_quick_stress(void) {
    print_colored("=== RUNNING BUMP ALLOCATOR QUICK STRESS TESTS ===\n", 0x0A, 0x00);
    
    if (test_framework_init() != 0) {
        print_colored("Failed to initialize test framework\n", 0x0C, 0x00);
        return -1;
    }
    
    // Run the most critical stress tests
    test_result_t results[3];
    const char *test_names[3] = {
        "Basic Stress",
        "Boundary Stress", 
        "Corruption Stress"
    };
    
    results[0] = test_bump_allocator_basic_stress();
    results[1] = test_bump_allocator_boundary_stress();
    results[2] = test_bump_allocator_corruption_stress();
    
    int passed = 0;
    int total = 3;
    
    for (int i = 0; i < total; i++) {
        if (results[i] == TEST_PASS) {
            print_colored("✓ ", 0x0A, 0x00);
            print(test_names[i]);
            print_colored(" PASSED\n", 0x0A, 0x00);
            passed++;
        } else {
            print_colored("✗ ", 0x0C, 0x00);
            print(test_names[i]);
            print_colored(" FAILED\n", 0x0C, 0x00);
        }
    }
    
    print_colored("\nQuick stress test summary: ", 0x0B, 0x00);
    print_uint(passed);
    print("/");
    print_uint(total);
    print(" tests passed\n");
    
    if (passed == total) {
        print_colored("All quick stress tests PASSED! 🎉\n", 0x0A, 0x00);
        return 0;
    } else {
        print_colored("Some quick stress tests FAILED! ⚠️\n", 0x0C, 0x00);
        return -1;
    }
}
