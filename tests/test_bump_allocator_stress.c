#include "test_suite.h"
#include <memory/bump_allocator.h>
#include <string.h>
#include <stdint.h>

// ===============================================================================
// BUMP ALLOCATOR STRESS TESTS
// ===============================================================================

// Test 1: Basic allocation stress test
test_result_t test_bump_allocator_basic_stress(void) {
    const int num_allocations = 1000;
    void *allocations[1000];
    
    // Allocate many small chunks
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 100) + 1; // Sizes from 1 to 100 bytes
        allocations[i] = kmalloc(size);
        
        if (allocations[i] == NULL) {
            test_log_error("Failed to allocate %zu bytes at iteration %d", size, i);
            return TEST_FAIL;
        }
        
        // Fill with pattern to detect corruption
        memset(allocations[i], i & 0xFF, size);
    }
    
    // Verify patterns
    for (int i = 0; i < num_allocations; i++) {
        size_t size = (i % 100) + 1;
        uint8_t *ptr = (uint8_t *)allocations[i];
        
        for (size_t j = 0; j < size; j++) {
            if (ptr[j] != (i & 0xFF)) {
                test_log_error("Memory corruption detected at allocation %d, offset %zu", i, j);
                return TEST_FAIL;
            }
        }
    }
    
    test_log_info("Basic stress test passed: %d allocations", num_allocations);
    return TEST_PASS;
}

// Test 2: Large allocation stress test
test_result_t test_bump_allocator_large_stress(void) {
    const int num_large_allocations = 50;
    void *allocations[50];
    
    // Allocate large chunks
    for (int i = 0; i < num_large_allocations; i++) 
    {
        size_t size = 1024 + (i * 256); // Sizes from 1KB to ~13KB
        allocations[i] = kmalloc(size);
        
        if (allocations[i] == NULL) 
        {
            test_log_error("Failed to allocate large chunk of %zu bytes at iteration %d", size, i);
            return TEST_FAIL;
        }
        
        // Fill with pattern
        memset(allocations[i], (i * 7) & 0xFF, size);
    }
    
    // Verify patterns
    for (int i = 0; i < num_large_allocations; i++) 
    {
        size_t size = 1024 + (i * 256);
        uint8_t *ptr = (uint8_t *)allocations[i];
        uint8_t expected = (i * 7) & 0xFF;
        
        for (size_t j = 0; j < size; j += 256) 
        { // Check every 256th byte to speed up
            if (ptr[j] != expected) {
                test_log_error("Large allocation corruption at %d, offset %zu", i, j);
                return TEST_FAIL;
            }
        }
    }
    
    test_log_info("Large allocation stress test passed: %d allocations", num_large_allocations);
    return TEST_PASS;
}

// Test 3: Alignment stress test
test_result_t test_bump_allocator_alignment_stress(void) {
    const int num_allocations = 200;
    void *allocations[200];
    
    // Test various sizes to stress alignment
    for (int i = 0; i < num_allocations; i++) 
    {
        size_t size = 1 + (i % 32); // Sizes from 1 to 32 bytes
        allocations[i] = kmalloc(size);
        
        if (allocations[i] == NULL) 
        {
            test_log_error("Failed to allocate %zu bytes at iteration %d", size, i);
            return TEST_FAIL;
        }
        
        // Check 16-byte alignment
        uintptr_t addr = (uintptr_t)allocations[i];
        if (addr % 16 != 0) 
        {
            test_log_error("Allocation not 16-byte aligned: %p", allocations[i]);
            return TEST_FAIL;
        }
        
        // Fill with pattern
        memset(allocations[i], i & 0xFF, size);
    }
    
    test_log_info("Alignment stress test passed: %d allocations", num_allocations);
    return TEST_PASS;
}

// Test 4: Mixed allocation sizes stress test
test_result_t test_bump_allocator_mixed_stress(void) {
    const int num_allocations = 500;
    void *allocations[500];
    size_t sizes[500];
    
    // Generate random-like sizes
    for (int i = 0; i < num_allocations; i++) {
        // Pseudo-random size generation
        sizes[i] = 1 + ((i * 1103515245 + 12345) % 2048);
        allocations[i] = kmalloc(sizes[i]);
        
        if (allocations[i] == NULL) {
            test_log_error("Failed to allocate %zu bytes at iteration %d", sizes[i], i);
            return TEST_FAIL;
        }
        
        // Fill with unique pattern
        memset(allocations[i], (i * 13 + 7) & 0xFF, sizes[i]);
    }
    
    // Verify all patterns
    for (int i = 0; i < num_allocations; i++) {
        uint8_t *ptr = (uint8_t *)allocations[i];
        uint8_t expected = (i * 13 + 7) & 0xFF;
        
        for (size_t j = 0; j < sizes[i]; j += 64) { // Check every 64th byte
            if (ptr[j] != expected) {
                test_log_error("Mixed allocation corruption at %d, offset %zu", i, j);
                return TEST_FAIL;
            }
        }
    }
    
    test_log_info("Mixed allocation stress test passed: %d allocations", num_allocations);
    return TEST_PASS;
}

// Test 5: Boundary stress test
test_result_t test_bump_allocator_boundary_stress(void) {
    // Test allocations near the heap limit
    const size_t heap_size = 0x100000; // 1MB from the implementation
    size_t total_allocated = 0;
    int allocation_count = 0;
    
    // Allocate chunks until we're close to the limit
    while (total_allocated < heap_size - 1024) {
        size_t chunk_size = 512; // 512-byte chunks
        void *ptr = kmalloc(chunk_size);
        
        if (ptr == NULL) {
            test_log_error("Unexpected allocation failure at %zu bytes allocated", total_allocated);
            return TEST_FAIL;
        }
        
        total_allocated += chunk_size;
        allocation_count++;
        
        // Fill with pattern
        memset(ptr, allocation_count & 0xFF, chunk_size);
    }
    
    // Try to allocate one more chunk - should fail
    void *final_ptr = kmalloc(1024);
    if (final_ptr != NULL) {
        test_log_error("Expected allocation to fail near heap limit, but got %p", final_ptr);
        return TEST_FAIL;
    }
    
    test_log_info("Boundary stress test passed: %d allocations, %zu bytes", allocation_count, total_allocated);
    return TEST_PASS;
}

// Test 6: Zero size allocation test
test_result_t test_bump_allocator_zero_size(void) {
    // Test allocation of zero bytes
    void *ptr1 = kmalloc(0);
    void *ptr2 = kmalloc(0);
    
    if (ptr1 == NULL || ptr2 == NULL) {
        test_log_error("Zero-size allocation failed");
        return TEST_FAIL;
    }
    
    // Check that they're different addresses
    if (ptr1 == ptr2) {
        test_log_error("Zero-size allocations returned same address");
        return TEST_FAIL;
    }
    
    test_log_info("Zero-size allocation test passed");
    return TEST_PASS;
}

// Test 7: Memory corruption stress test
test_result_t test_bump_allocator_corruption_stress(void) {
    const int num_allocations = 100;
    void *allocations[100];
    
    // Allocate and fill with patterns
    for (int i = 0; i < num_allocations; i++) {
        size_t size = 64 + (i % 128);
        allocations[i] = kmalloc(size);
        
        if (allocations[i] == NULL) {
            test_log_error("Failed to allocate %zu bytes at iteration %d", size, i);
            return TEST_FAIL;
        }
        
        // Fill with unique pattern
        uint8_t *ptr = (uint8_t *)allocations[i];
        for (size_t j = 0; j < size; j++) {
            ptr[j] = (i + j) & 0xFF;
        }
    }
    
    // Verify all patterns multiple times
    for (int verify_round = 0; verify_round < 3; verify_round++) {
        for (int i = 0; i < num_allocations; i++) {
            size_t size = 64 + (i % 128);
            uint8_t *ptr = (uint8_t *)allocations[i];
            
            for (size_t j = 0; j < size; j++) {
                if (ptr[j] != ((i + j) & 0xFF)) {
                    test_log_error("Memory corruption detected at allocation %d, offset %zu, round %d", 
                                  i, j, verify_round);
                    return TEST_FAIL;
                }
            }
        }
    }
    
    test_log_info("Memory corruption stress test passed: %d allocations, 3 verification rounds", num_allocations);
    return TEST_PASS;
}

// Test 8: Rapid allocation/deallocation stress test
test_result_t test_bump_allocator_rapid_stress(void) {
    const int num_iterations = 1000;
    
    for (int iter = 0; iter < num_iterations; iter++) {
        // Allocate a small chunk
        void *ptr = kmalloc(16);
        if (ptr == NULL) {
            test_log_error("Rapid allocation failed at iteration %d", iter);
            return TEST_FAIL;
        }
        
        // Fill with iteration number
        memset(ptr, iter & 0xFF, 16);
        
        // Verify immediately
        uint8_t *check_ptr = (uint8_t *)ptr;
        for (int j = 0; j < 16; j++) {
            if (check_ptr[j] != (iter & 0xFF)) {
                test_log_error("Rapid allocation corruption at iteration %d, offset %d", iter, j);
                return TEST_FAIL;
            }
        }
        
        // Note: kfree() doesn't actually free memory in bump allocator
        kfree(ptr);
    }
    
    test_log_info("Rapid allocation stress test passed: %d iterations", num_iterations);
    return TEST_PASS;
}

// ===============================================================================
// TEST REGISTRATION
// ===============================================================================

int test_bump_allocator_stress_suite_init(void) {
    test_register_suite("Bump Allocator Stress", "Stress tests for bump allocator");
    
    test_case_t bump_stress_tests[] = {
        TEST_CASE("Basic Stress", "Basic stress test with many small allocations", 
                 test_bump_allocator_basic_stress, TEST_CAT_STRESS, TEST_LEVEL_HIGH),
        TEST_CASE("Large Stress", "Stress test with large allocations", 
                 test_bump_allocator_large_stress, TEST_CAT_STRESS, TEST_LEVEL_HIGH),
        TEST_CASE("Alignment Stress", "Stress test for memory alignment", 
                 test_bump_allocator_alignment_stress, TEST_CAT_STRESS, TEST_LEVEL_MEDIUM),
        TEST_CASE("Mixed Stress", "Stress test with mixed allocation sizes", 
                 test_bump_allocator_mixed_stress, TEST_CAT_STRESS, TEST_LEVEL_HIGH),
        TEST_CASE("Boundary Stress", "Stress test near heap boundaries", 
                 test_bump_allocator_boundary_stress, TEST_CAT_STRESS, TEST_LEVEL_CRITICAL),
        TEST_CASE("Zero Size", "Test zero-size allocations", 
                 test_bump_allocator_zero_size, TEST_CAT_UNIT, TEST_LEVEL_MEDIUM),
        TEST_CASE("Corruption Stress", "Stress test for memory corruption detection", 
                 test_bump_allocator_corruption_stress, TEST_CAT_STRESS, TEST_LEVEL_CRITICAL),
        TEST_CASE("Rapid Stress", "Rapid allocation/deallocation stress test", 
                 test_bump_allocator_rapid_stress, TEST_CAT_STRESS, TEST_LEVEL_HIGH)
    };
    
    for (int i = 0; i < sizeof(bump_stress_tests) / sizeof(test_case_t); i++) {
        test_register_case("Bump Allocator Stress", &bump_stress_tests[i]);
    }
    
    return 0;
}
