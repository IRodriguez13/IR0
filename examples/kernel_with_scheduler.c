// ===============================================================================
// EXAMPLE: Kernel with Scheduler Enabled
// ===============================================================================
// This file shows how to configure the kernel with scheduler and process management
// Usage: Copy this configuration to kernel_start.c when scheduler is implemented

#include "kernel_start.h"

// ===============================================================================
// KERNEL CONFIGURATION WITH SCHEDULER
// ===============================================================================

// Enable scheduler and process management
#define KERNEL_CONFIG_CUSTOM

// Custom configuration with scheduler
#define ENABLE_BUMP_ALLOCATOR     1
#define ENABLE_HEAP_ALLOCATOR     1      // Required for scheduler
#define ENABLE_PHYSICAL_ALLOCATOR 0
#define ENABLE_VIRTUAL_MEMORY     0
#define ENABLE_PROCESS_MANAGEMENT 1      // Enable process management
#define ENABLE_ELF_LOADER         1      // Enable ELF loading
#define ENABLE_SCHEDULER          1      // Enable scheduler
#define ENABLE_SYSCALLS           1      // Enable system calls
#define ENABLE_VFS                0      // No file system for now
#define ENABLE_IR0FS              0
#define ENABLE_SHELL              0      // No shell for now
#define ENABLE_KEYBOARD_DRIVER    1
#define ENABLE_ATA_DRIVER         1
#define ENABLE_PS2_DRIVER         1
#define ENABLE_TIMER_DRIVERS      1
#define ENABLE_DEBUGGING          1
#define ENABLE_LOGGING            1

// Scheduler specific configuration
#define SCHEDULER_TYPE_ROUND_ROBIN    1
#define SCHEDULER_TYPE_PRIORITY       1
#define SCHEDULER_TYPE_CFS            0  // Disable CFS for now
#define SCHEDULER_MAX_TASKS           16 // Start with few tasks
#define SCHEDULER_TIME_SLICE          10 // 10ms time slice

#include <ir0/kernel_config_advanced.h>
#include <ir0/kernel_includes.h>

// ===============================================================================
// EXAMPLE INITIALIZATION WITH SCHEDULER
// ===============================================================================

void main(void)
{
    // Banner de inicio
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    IR0 Kernel v0.0.0                         ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    Build: SCHEDULER                          ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // 0. Inicializar sistema de logging
    logging_init();
    logging_set_level(LOG_LEVEL_INFO);
    log_info("KERNEL", "System initialization started");
    
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
    
    // Initialize heap allocator (required for scheduler)
    if (ENABLE_HEAP_ALLOCATOR) {
        // heap_allocator_init();  // TODO: Implement
        log_info("KERNEL", "Heap allocator initialized");
    }
    
    log_info("KERNEL", "Memory management initialized");
    delay_ms(1500);

    // 3. Inicializar sistema de procesos
    if (ENABLE_PROCESS_MANAGEMENT) {
        log_info("KERNEL", "Initializing process management");
        // process_init();  // TODO: Implement
        log_info("KERNEL", "Process management initialized");
        delay_ms(1500);
    }

    // 4. Inicializar scheduler
    if (ENABLE_SCHEDULER) {
        log_info("KERNEL", "Initializing task scheduler");
        // scheduler_init();  // TODO: Implement
        log_info("KERNEL", "Task scheduler initialized");
        delay_ms(1500);
    }

    // 5. Inicializar system calls
    if (ENABLE_SYSCALLS) {
        log_info("KERNEL", "Initializing system call interface");
        // syscalls_init();  // TODO: Implement
        log_info("KERNEL", "System call interface initialized");
        delay_ms(1500);
    }

    // 6. Inicializar timer system
    log_info("KERNEL", "Initializing timer system");
    init_clock();
    log_info("KERNEL", "Timer system initialized");
    delay_ms(1500);

    // 7. Inicializar drivers de hardware
    log_info("KERNEL", "Initializing hardware drivers");

    // Inicializar driver de teclado personalizado
    keyboard_init();
    log_info("KERNEL", "Keyboard driver initialized");

    // Inicializar driver de disco ATA
    ata_init();
    log_info("KERNEL", "ATA disk driver initialized");

    delay_ms(1500);

    // 8. Habilitar interrupciones
    __asm__ volatile("sti");
    log_info("KERNEL", "All interrupts enabled");
    delay_ms(1500);

    // Banner de sistema listo
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                        SYSTEM READY                          ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("║                 All subsystems initialized                   ║\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(1500);

    log_info("KERNEL", "Kernel initialization completed successfully");
    log_info("KERNEL", "System running with scheduler enabled");

    // 9. Crear tareas de ejemplo (cuando scheduler esté implementado)
    if (ENABLE_SCHEDULER) {
        log_info("KERNEL", "Creating example tasks");
        
        // Example task creation (when implemented):
        // task_t *task1 = create_task("task1", task1_function, PRIORITY_NORMAL);
        // task_t *task2 = create_task("task2", task2_function, PRIORITY_HIGH);
        // task_t *task3 = create_task("task3", task3_function, PRIORITY_LOW);
        
        // add_task_to_scheduler(task1);
        // add_task_to_scheduler(task2);
        // add_task_to_scheduler(task3);
        
        log_info("KERNEL", "Example tasks created");
    }

    // 10. Iniciar scheduler (cuando esté implementado)
    if (ENABLE_SCHEDULER) {
        log_info("KERNEL", "Starting scheduler");
        // start_scheduler();  // This should never return
    }

    // Loop infinito de respaldo (si scheduler no está implementado)
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

// ===============================================================================
// EXAMPLE TASK FUNCTIONS (for when scheduler is implemented)
// ===============================================================================

/*
// Example task functions (uncomment when scheduler is implemented)
void task1_function(void) {
    while (1) {
        log_info("TASK1", "Task 1 running");
        // task_yield();  // Yield to other tasks
        for (volatile int i = 0; i < 1000000; i++) { /* busy wait */ }
    }
}

void task2_function(void) {
    while (1) {
        log_info("TASK2", "Task 2 running");
        // task_yield();  // Yield to other tasks
        for (volatile int i = 0; i < 2000000; i++) { /* busy wait */ }
    }
}

void task3_function(void) {
    while (1) {
        log_info("TASK3", "Task 3 running");
        // task_yield();  // Yield to other tasks
        for (volatile int i = 0; i < 1500000; i++) { /* busy wait */ }
    }
}
*/
