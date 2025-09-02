// ===============================================================================
// EXAMPLE: Bump Allocator Testing with Interrupts
// ===============================================================================
// This configuration is optimized for testing the bump allocator with interrupts
// Usage: Copy this configuration to kernel_start.c for bump allocator testing

#include "kernel_start.h"

// ===============================================================================
// BUMP ALLOCATOR TESTING CONFIGURATION
// ===============================================================================

// Enable development mode for testing
#define IR0_DEVELOPMENT_MODE

// Override specific settings for bump allocator testing
#undef ENABLE_MEMORY_TESTS
#undef ENABLE_STRESS_TESTS
#undef ENABLE_DEBUGGING

#define ENABLE_MEMORY_TESTS       1   // Enable memory tests
#define ENABLE_STRESS_TESTS       1   // Enable stress tests
#define ENABLE_DEBUGGING          1   // Enable debugging

// Ensure interrupts are enabled
#undef ENABLE_KEYBOARD_DRIVER
#undef ENABLE_TIMER_DRIVERS

#define ENABLE_KEYBOARD_DRIVER    1   // Required for interrupt testing
#define ENABLE_TIMER_DRIVERS      1   // Required for interrupt testing

#include <ir0/kernel_includes.h>

// ===============================================================================
// BUMP ALLOCATOR TESTING INITIALIZATION
// ===============================================================================

void main(void)
{
    // Banner de inicio
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    IR0 Kernel v0.0.0  pre-release           ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    Build: BUMP-ALLOCATOR-TESTING            ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // 0. Inicializar sistema de logging
    logging_init();
    logging_set_level(LOG_LEVEL_INFO);
    log_info("KERNEL", "Bump Allocator Testing Mode Started");
    
    // Display kernel configuration
    log_info("KERNEL", "Kernel Configuration:");
    log_info_fmt("KERNEL", "  Build Type: %s", KERNEL_BUILD_TYPE);
    log_info_fmt("KERNEL", "  Memory Management: %s", HAS_MEMORY_MANAGEMENT() ? "ENABLED" : "DISABLED");
    log_info_fmt("KERNEL", "  Process Management: %s", HAS_PROCESS_MANAGEMENT() ? "ENABLED" : "DISABLED");
    log_info_fmt("KERNEL", "  File System: %s", HAS_FILE_SYSTEM() ? "ENABLED" : "DISABLED");
    log_info_fmt("KERNEL", "  Drivers: %s", HAS_DRIVERS() ? "ENABLED" : "DISABLED");
    log_info_fmt("KERNEL", "  Debugging: %s", HAS_DEBUGGING() ? "ENABLED" : "DISABLED");

    delay_ms(1500);

    // 1. Inicializar IDT y sistema de interrupciones
    log_info("KERNEL", "Initializing interrupt system for testing");
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
    log_info("KERNEL", "Memory management initialized (using bump_allocator only)");
    delay_ms(1500);

    // 3. Inicializar timer system
    log_info("KERNEL", "Initializing timer system");
    init_clock();
    log_info("KERNEL", "Timer system initialized");
    delay_ms(1500);

    // 4. Inicializar drivers de hardware
    log_info("KERNEL", "Initializing hardware drivers");

    // Inicializar driver de teclado personalizado
    keyboard_init();
    log_info("KERNEL", "Keyboard driver initialized");

    // Inicializar driver de disco ATA
    ata_init();
    log_info("KERNEL", "ATA disk driver initialized");

    delay_ms(1500);

    // 5. Habilitar interrupciones
    __asm__ volatile("sti");
    log_info("KERNEL", "All interrupts enabled");
    delay_ms(1500);

    // Banner de sistema listo
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                    BUMP ALLOCATOR TESTING READY              ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                 Interrupts + Memory Testing                  ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(1500);

    log_info("KERNEL", "Bump allocator testing mode ready");
    log_info("KERNEL", "System running with interrupts + memory testing");

    // ===============================================================================
    // BUMP ALLOCATOR STRESS TEST WITH INTERRUPTS
    // ===============================================================================
    log_info("KERNEL", "Starting bump allocator stress test with interrupts...");
    
    // Test 1: Basic allocations with interrupts enabled
    log_info("KERNEL", "Test 1: Basic allocations with interrupts enabled");
    void *ptr1 = kmalloc(16);
    void *ptr2 = kmalloc(32);
    void *ptr3 = kmalloc(64);
    
    if (ptr1 && ptr2 && ptr3) 
    {
        log_info("KERNEL", "✓ Basic allocations successful with interrupts");
    } 
    else 
    {
        log_error("KERNEL", "✗ Basic allocations failed with interrupts");
    }
    
    // Test 2: Memory patterns with interrupts
    log_info("KERNEL", "Test 2: Memory patterns with interrupts");
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
    
    if (pattern_ok) 
    {
        log_info("KERNEL", "✓ Memory patterns verified with interrupts");
    } 
    else 
    {
        log_error("KERNEL", "✗ Memory pattern corruption with interrupts");
    }
    
    // Test 3: Alignment with interrupts
    log_info("KERNEL", "Test 3: Memory alignment with interrupts");
    uintptr_t addr1 = (uintptr_t)ptr1;
    uintptr_t addr2 = (uintptr_t)ptr2;
    uintptr_t addr3 = (uintptr_t)ptr3;
    
    if ((addr1 % 16 == 0) && (addr2 % 16 == 0) && (addr3 % 16 == 0)) 
    {
        log_info("KERNEL", "✓ Memory alignment correct with interrupts");
    } 
    else 
    {
        log_error("KERNEL", "✗ Memory alignment incorrect with interrupts");
    }
    
    // Test 4: Many small allocations with interrupts
    log_info("KERNEL", "Test 4: Many small allocations with interrupts");
    void *small_ptrs[50];
    int success_count = 0;
    
    for (int i = 0; i < 50; i++) 
    {
        small_ptrs[i] = kmalloc(8);
        if (small_ptrs[i]) 
        {
            memset(small_ptrs[i], i & 0xFF, 8);
            success_count++;
        }
    }
    
    log_info_fmt("KERNEL", "✓ %d/50 small allocations successful with interrupts", success_count);
    
    // Test 5: Large allocation with interrupts
    log_info("KERNEL", "Test 5: Large allocation with interrupts");
    void *large_ptr = kmalloc(1024);
    if (large_ptr) 
    {
        memset(large_ptr, 0xDD, 1024);
        log_info("KERNEL", "✓ Large allocation successful with interrupts");
    } 
    else 
    {
        log_error("KERNEL", "✗ Large allocation failed with interrupts");
    }
    
    // Test 6: Stress test with interrupts
    log_info("KERNEL", "Test 6: Stress test with interrupts");
    void *stress_ptrs[100];
    int stress_success = 0;
    
    for (int i = 0; i < 100; i++) 
    {
        size_t size = (i % 100) + 1; // Sizes from 1 to 100 bytes
        stress_ptrs[i] = kmalloc(size);
        if (stress_ptrs[i]) 
        {
            memset(stress_ptrs[i], (i * 7) & 0xFF, size);
            stress_success++;
        }
    }
    
    log_info_fmt("KERNEL", "✓ %d/100 stress allocations successful with interrupts", stress_success);
    
    // Test 7: Verify stress allocations with interrupts
    log_info("KERNEL", "Test 7: Verifying stress allocations with interrupts");
    int corruption_count = 0;
    
    for (int i = 0; i < stress_success; i++) 
    {
        size_t size = (i % 100) + 1;
        uint8_t *ptr = (uint8_t *)stress_ptrs[i];
        uint8_t expected = (i * 7) & 0xFF;
        
        for (size_t j = 0; j < size; j++) 
        {
            if (ptr[j] != expected) 
            {
                corruption_count++;
                break;
            }
        }
    }
    
    if (corruption_count == 0) 
    {
        log_info("KERNEL", "✓ No memory corruption with interrupts");
    } 
    else 
    {
        log_error_fmt("KERNEL", "✗ Memory corruption detected in %d allocations with interrupts", corruption_count);
    }
    
    // Final summary
    log_info("KERNEL", "=== BUMP ALLOCATOR STRESS TEST WITH INTERRUPTS COMPLETED ===");
    if (stress_success == 100 && corruption_count == 0) 
    {
        log_info("KERNEL", "🎉 ALL TESTS PASSED! Bump allocator working correctly with interrupts");
    } 
    else 
    {
        log_error("KERNEL", "⚠️ SOME TESTS FAILED! Bump allocator has issues with interrupts");
    }
    
    delay_ms(2000);

    // Loop infinito con interrupciones habilitadas
    log_info("KERNEL", "Entering main loop with interrupts enabled");
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
