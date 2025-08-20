// memory/heap_test.c - Test del Heap Dinámico
#include "heap_allocator.h"
#include "memo_interface.h"
#include "../includes/ir0/print.h"
#include "../includes/string.h"
#include <stdbool.h>

// ===============================================================================
// TEST FUNCTIONS
// ===============================================================================

void test_heap_initialization(void)
{
    print_colored("=== TEST: Heap Initialization ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Inicializar sistema de memoria
    memory_init();
    
    // Verificar que el heap está inicializado
    uint32_t total_pages, used_pages;
    size_t total_bytes, used_bytes, free_bytes;
    
    heap_get_stats(&total_pages, &used_pages, &total_bytes, &used_bytes, &free_bytes);
    
    print("Total pages: ");
    print_uint32(total_pages);
    print("\n");
    
    print("Total bytes: ");
    print_uint32(total_bytes / 1024);
    print(" KB\n");
    
    print("Free bytes: ");
    print_uint32(free_bytes);
    print("\n");
    
    if (total_pages > 0 && total_bytes > 0) {
        print_colored("✓ Heap initialization test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    } else {
        print_colored("✗ Heap initialization test FAILED\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
    
    print("\n");
}

void test_basic_allocation(void)
{
    print_colored("=== TEST: Basic Allocation ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Allocar diferentes tamaños
    void *ptr1 = kmalloc(1024);
    void *ptr2 = kmalloc(2048);
    void *ptr3 = kmalloc(512);
    
    print("Allocated ptr1: 0x");
    print_hex_compact((uintptr_t)ptr1);
    print("\n");
    
    print("Allocated ptr2: 0x");
    print_hex_compact((uintptr_t)ptr2);
    print("\n");
    
    print("Allocated ptr3: 0x");
    print_hex_compact((uintptr_t)ptr3);
    print("\n");
    
    // Verificar que las direcciones están en la región correcta
    bool valid_addresses = true;
    
    if (ptr1 && ((uintptr_t)ptr1 >= KERNEL_HEAP_BASE && (uintptr_t)ptr1 < KERNEL_HEAP_END)) {
        print_colored("✓ ptr1 address valid\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    } else {
        print_colored("✗ ptr1 address invalid\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        valid_addresses = false;
    }
    
    if (ptr2 && ((uintptr_t)ptr2 >= KERNEL_HEAP_BASE && (uintptr_t)ptr2 < KERNEL_HEAP_END)) {
        print_colored("✓ ptr2 address valid\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    } else {
        print_colored("✗ ptr2 address invalid\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        valid_addresses = false;
    }
    
    if (ptr3 && ((uintptr_t)ptr3 >= KERNEL_HEAP_BASE && (uintptr_t)ptr3 < KERNEL_HEAP_END)) {
        print_colored("✓ ptr3 address valid\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    } else {
        print_colored("✗ ptr3 address invalid\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        valid_addresses = false;
    }
    
    if (valid_addresses) {
        print_colored("✓ Basic allocation test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    } else {
        print_colored("✗ Basic allocation test FAILED\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
    
    // Liberar memoria
    kfree(ptr1);
    kfree(ptr2);
    kfree(ptr3);
    
    print("\n");
}

void test_heap_growth(void)
{
    print_colored("=== TEST: Heap Growth ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Obtener estadísticas iniciales
    uint32_t initial_pages, initial_used_pages;
    size_t initial_total_bytes, initial_used_bytes, initial_free_bytes;
    
    heap_get_stats(&initial_pages, &initial_used_pages, &initial_total_bytes, &initial_used_bytes, &initial_free_bytes);
    
    print("Initial pages: ");
    print_uint32(initial_pages);
    print("\n");
    
    print("Initial free bytes: ");
    print_uint32(initial_free_bytes);
    print("\n");
    
    // Allocar una cantidad grande para forzar el crecimiento
    void *large_ptr = kmalloc(64 * 1024); // 64KB
    
    if (large_ptr) {
        print("Large allocation successful: 0x");
        print_hex_compact((uintptr_t)large_ptr);
        print("\n");
        
        // Obtener estadísticas después del crecimiento
        uint32_t final_pages, final_used_pages;
        size_t final_total_bytes, final_used_bytes, final_free_bytes;
        
        heap_get_stats(&final_pages, &final_used_pages, &final_total_bytes, &final_used_bytes, &final_free_bytes);
        
        print("Final pages: ");
        print_uint32(final_pages);
        print("\n");
        
        print("Final total bytes: ");
        print_uint32(final_total_bytes / 1024);
        print(" KB\n");
        
        if (final_pages > initial_pages) {
            print_colored("✓ Heap growth test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
            print("Heap grew from ");
            print_uint32(initial_pages);
            print(" to ");
            print_uint32(final_pages);
            print(" pages\n");
        } else {
            print_colored("✗ Heap growth test FAILED\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        }
        
        kfree(large_ptr);
    } else {
        print_colored("✗ Large allocation failed\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
    
    print("\n");
}

void test_memory_corruption(void)
{
    print_colored("=== TEST: Memory Corruption Detection ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Allocar memoria
    void *ptr = kmalloc(1024);
    
    if (ptr) {
        // Escribir en la memoria
        memset(ptr, 0xAA, 1024);
        
        // Verificar que se escribió correctamente
        uint8_t *bytes = (uint8_t *)ptr;
        bool corruption = false;
        
        for (int i = 0; i < 1024; i++) {
            if (bytes[i] != 0xAA) {
                corruption = true;
                break;
            }
        }
        
        if (!corruption) {
            print_colored("✓ Memory write/read test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        } else {
            print_colored("✗ Memory write/read test FAILED\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
        }
        
        kfree(ptr);
    } else {
        print_colored("✗ Memory allocation failed\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
    
    print("\n");
}

void test_fragmentation(void)
{
    print_colored("=== TEST: Memory Fragmentation ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Allocar múltiples bloques pequeños
    void *ptrs[10];
    
    for (int i = 0; i < 10; i++) {
        ptrs[i] = kmalloc(256);
        if (!ptrs[i]) {
            print_colored("✗ Failed to allocate block ");
            print_uint32(i);
            print("\n");
            return;
        }
    }
    
    // Liberar bloques alternos
    for (int i = 0; i < 10; i += 2) {
        kfree(ptrs[i]);
        ptrs[i] = NULL;
    }
    
    // Intentar allocar un bloque grande
    void *large_ptr = kmalloc(2048);
    
    if (large_ptr) {
        print_colored("✓ Fragmentation test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
        kfree(large_ptr);
    } else {
        print_colored("✗ Fragmentation test FAILED\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
    
    // Liberar bloques restantes
    for (int i = 1; i < 10; i += 2) {
        if (ptrs[i]) {
            kfree(ptrs[i]);
        }
    }
    
    print("\n");
}

void test_heap_debug(void)
{
    print_colored("=== TEST: Heap Debug Information ===\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Mostrar información del heap
    debug_heap_allocator();
    
    print_colored("✓ Heap debug test PASSED\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print("\n");
}

// ===============================================================================
// MAIN TEST FUNCTION
// ===============================================================================

void run_heap_tests(void)
{
    print_colored("=== RUNNING HEAP ALLOCATOR TESTS ===\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
    print("\n");
    
    test_heap_initialization();
    test_basic_allocation();
    test_heap_growth();
    test_memory_corruption();
    test_fragmentation();
    test_heap_debug();
    
    print_colored("=== HEAP TESTS COMPLETED ===\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
    print("\n");
}
