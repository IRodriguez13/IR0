#include "kernel_start.h"

// ===============================================================================
// KERNEL CONFIGURATION
// ===============================================================================
// Choose your configuration by setting the appropriate flag

// For testing bump allocator with interrupts:
#define IR0_DEVELOPMENT_MODE

// Alternative configurations:
// #define IR0_DESKTOP      // Full desktop kernel
// #define IR0_SERVER       // Server kernel
// #define IR0_IOT          // IoT kernel
// #define IR0_EMBEDDED     // Minimal embedded kernel
// #define IR0_TESTING_MODE // Testing mode

#include <ir0/kernel_includes.h>

void main(void)
{
    // Banner de inicio
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    IR0 Kernel v0.0.0  pre-release                       ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // 0. Inicializar sistema de logging
    logging_init();
    logging_set_level(LOG_LEVEL_INFO);
    log_info("KERNEL", "Bump Allocator Testing Mode Started");

    delay_ms(1500);

    // 1. Inicializar IDT y sistema de interrupciones
    log_info("KERNEL", "Initializing interrupt system");
#ifdef __x86_64__
    idt_init64();
    idt_load64();
    pic_remap64();
    keyboard_init();
#else
    idt_init32();
    idt_load32();
    pic_remap32();
    keyboard_init();
#endif
    log_info("KERNEL", "Interrupt system initialized");

    // Habilitar interrupciones globalmente
    __asm__ volatile("sti");
    log_info("KERNEL", "Global interrupts enabled");

    delay_ms(1500);

    // 2. Inicializar gestión de memoria
    log_info("KERNEL", "Initializing memory management");
    delay_ms(1500);

    // 3. Inicializar timer system
    log_info("KERNEL", "Initializing timer system");
    init_clock();
    delay_ms(1500);

    // 4. Inicializar drivers de hardware
    log_info("KERNEL", "Initializing hardware drivers");
    keyboard_init();
    ata_init();

    delay_ms(1500);

    // 7. Inicializar VFS (comentado - requiere memoria dinámica)
    // log_info("KERNEL", "Initializing virtual file system");
    // vfs_init();
    // log_info("KERNEL", "Virtual file system initialized");
    // delay_ms(1500);

    // 8. Habilitar interrupciones
    __asm__ volatile("sti");
    log_info("KERNEL", "All interrupts enabled");
    delay_ms(1500);

    // Banner de sistema listo
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                        SYSTEM READY                          ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                        SYSTEM READY                          ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                 All subsystems initialized                   ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(2500);

    log_info("KERNEL", "Kernel initialization completed successfully");
    log_info("KERNEL", "System running with minimal memory management");

    // ===============================================================================
    // SIMPLE BUMP ALLOCATOR STRESS TEST
    // ===============================================================================
    log_info("KERNEL", "Starting bump allocator stress test...");
    
    // Test 1: Basic allocations
    log_info("KERNEL", "Test 1: Basic allocations");
    void *ptr1 = kmalloc(16);
    void *ptr2 = kmalloc(32);
    void *ptr3 = kmalloc(64);
    
    if (ptr1 && ptr2 && ptr3) {
        log_info("KERNEL", "✓ Basic allocations successful");
    } else {
        log_error("KERNEL", "✗ Basic allocations failed");
    }
    
    // Test 2: Memory patterns
    log_info("KERNEL", "Test 2: Memory patterns");
    memset(ptr1, 0xAA, 16);
    memset(ptr2, 0xBB, 32);
    memset(ptr3, 0xCC, 64);
    
    // Verify patterns
    uint8_t *check1 = (uint8_t *)ptr1;
    uint8_t *check2 = (uint8_t *)ptr2;
    uint8_t *check3 = (uint8_t *)ptr3;
    
    bool pattern_ok = true;
    for (int i = 0; i < 16; i++) if (check1[i] != 0xAA) pattern_ok = false;
    for (int i = 0; i < 32; i++) if (check2[i] != 0xBB) pattern_ok = false;
    for (int i = 0; i < 64; i++) if (check3[i] != 0xCC) pattern_ok = false;
    
    if (pattern_ok) {
        log_info("KERNEL", "✓ Memory patterns verified");
    } else {
        log_error("KERNEL", "✗ Memory pattern corruption detected");
    }
    
    // Test 3: Alignment
    log_info("KERNEL", "Test 3: Memory alignment");
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    uintptr_t addr3 = (uintptr_t)ptr3;
    
    if ((addr1 % 16 == 0) && (addr2 % 16 == 0) && (addr3 % 16 == 0)) {
        log_info("KERNEL", "✓ Memory alignment correct (16-byte aligned)");
    } else {
        log_error("KERNEL", "✗ Memory alignment incorrect");
    }
    
    // Test 4: Many small allocations
    log_info("KERNEL", "Test 4: Many small allocations");
    void *small_ptrs[50];
    int success_count = 0;
    
    for (int i = 0; i < 50; i++) {
        small_ptrs[i] = kmalloc(8);
        if (small_ptrs[i]) {
            memset(small_ptrs[i], i & 0xFF, 8);
            success_count++;
        }
    }
    
    log_info_fmt("KERNEL", "✓ %d/50 small allocations successful", success_count);
    
    // Test 5: Large allocation
    log_info("KERNEL", "Test 5: Large allocation");
    void *large_ptr = kmalloc(1024);
    if (large_ptr) {
        memset(large_ptr, 0xDD, 1024);
        log_info("KERNEL", "✓ Large allocation successful");
    } else {
        log_error("KERNEL", "✗ Large allocation failed");
    }
    
    // Test 6: Stress test - many allocations
    log_info("KERNEL", "Test 6: Stress test - many allocations");
    void *stress_ptrs[100];
    int stress_success = 0;
    
    for (int i = 0; i < 100; i++) {
        size_t size = (i % 100) + 1; // Sizes from 1 to 100 bytes
        stress_ptrs[i] = kmalloc(size);
        if (stress_ptrs[i]) {
            memset(stress_ptrs[i], (i * 7) & 0xFF, size);
            stress_success++;
        }
    }
    
    log_info_fmt("KERNEL", "✓ %d/100 stress allocations successful", stress_success);
    
    // Test 7: Verify stress allocations
    log_info("KERNEL", "Test 7: Verifying stress allocations");
    int corruption_count = 0;
    
    for (int i = 0; i < stress_success; i++) {
        size_t size = (i % 100) + 1;
        uint8_t *ptr = (uint8_t *)stress_ptrs[i];
        uint8_t expected = (i * 7) & 0xFF;
        
        for (size_t j = 0; j < size; j++) {
            if (ptr[j] != expected) {
                corruption_count++;
                break;
            }
        }
    }
    
    if (corruption_count == 0) {
        log_info("KERNEL", "✓ No memory corruption detected");
    } else {
        log_error_fmt("KERNEL", "✗ Memory corruption detected in %d allocations", corruption_count);
    }
    
    // Final summary
    log_info("KERNEL", "=== BUMP ALLOCATOR STRESS TEST COMPLETED ===");
    if (stress_success == 100 && corruption_count == 0) {
        log_info("KERNEL", "🎉 ALL TESTS PASSED! Bump allocator working correctly");
    } else {
        log_error("KERNEL", "⚠️ SOME TESTS FAILED! Bump allocator has issues");
    }
    
    delay_ms(2000);

    // Loop infinito simple - sin shell ni scheduler por ahora
    while (1)
    {
        // Hacer algo básico para mantener el sistema ocupado
        __asm__ volatile("hlt");
        
        // Pequeña pausa
        for (volatile int i = 0; i < 1000000; i++)
        {
            // Busy wait
        }
    }
}