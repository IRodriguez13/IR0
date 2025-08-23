#include <ir0/kernel.h>
#include <ir0/print.h>
#include <ir0/logging.h>
#include <ir0/stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "../arch/common/arch_interface.h"
#include "../arch/common/idt.h"
#include "../memory/memo_interface.h"
#include "../drivers/timer/clock_system.h"
#include "../fs/vfs.h"
#include "../fs/ir0fs.h"
#include "../kernel/scheduler/scheduler.h"
#include "../kernel/shell/shell.h"
#include "../setup/kernel_config.h"
#include "../memory/heap_allocator.h"
#include "../memory/physical_allocator.h"
#include "../kernel/process/process.h"
#include "../kernel/syscalls/syscalls.h"

#ifdef __x86_64__

#include "../arch/x86-64/sources/tss_x64.h"

#endif

#ifdef __x86_64__

#include "../memory/arch/x86-64/Paging_x64.h"

#else

#include "../memory/arch/x_86-32/Paging_x86-32.h"

#endif

// Variables globales para debugging
volatile uint64_t *debug_ptr = (uint64_t *)0x100000;

void main(void)
{
    // Inicialización básica del kernel
    print_colored("=== IR0 Kernel Starting ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(2000);

    // SISTEMA DE I/O COMPLETO
    print_success("[OK] Initializing I/O subsystem...\n");

    // 1. PS/2 Keyboard
    ps2_init();
    print_success("[OK] PS/2 keyboard initialized\n");
    
    // Habilitar explícitamente IRQ1 (teclado) en el PIC
    pic_unmask_irq(1);
    print_success("[OK] Keyboard IRQ1 enabled in PIC\n");

    // 2. PS/2 Mouse (si está disponible) - comentar por ahora
    // if (ps2_mouse_init() == 0) {
    //     print_success("[OK] PS/2 mouse initialized\n");
    // } else {
    //     print_warning("[WARN] PS/2 mouse not found\n");
    // }

    delay_ms(1000);

    // SISTEMA DE ARCHIVOS BÁSICO
    print_success("[OK] Initializing file system subsystem...\n");

    // 1. ATA Disk Driver
    ata_init();
    print_success("[OK] ATA disk driver initialized\n");

    // 2. VFS Simple (ya incluido en el build)
    vfs_simple_init();
    print_success("[OK] VFS Simple initialized\n");

    delay_ms(1000);

    // SISTEMA DE SCHEDULER CON DETECCIÓN AUTOMÁTICA
    print_success("[OK] Initializing scheduler subsystem with auto-detection...\n");
    
    // Usar el sistema de detección automática de schedulers
    extern int scheduler_cascade_init(void);
    if (scheduler_cascade_init() != 0) {
        print_error("[ERROR] Scheduler auto-detection failed!\n");
        panic("Scheduler initialization failed");
    }
    
    print_success("[OK] Scheduler auto-detection completed\n");
    
    delay_ms(1000);
}

static void enable_interrupts(void)
{
    print_success("[OK] Interrupt system ready\n");

    // Delay for visual effect
    delay_ms(1500);

    // Habilitar interrupciones - AHORA CON STACK MAPEADO
    __asm__ volatile("sti");
    interrupts_enabled = true;

    print_success("[OK] Global interrupts enabled\n");

    // Delay for visual effect
    delay_ms(1500);
}

// ===============================================================================
// AUTHENTICATION INITIALIZATION
// ===============================================================================

static void init_auth_system(void)
{
    auth_config_t config;
    config.max_attempts = 3;
    config.lockout_time = 0;
    config.require_password = false;
    config.case_sensitive = true;
    
    if (auth_init(&config) != 0) {
        print_error("[ERROR] Failed to initialize authentication system\n");
        panic("Authentication system initialization failed");
    }
    
    print_success("[OK] Authentication system initialized\n");
}

// ===============================================================================
// USER SPACE PROCESS IMPLEMENTATION
// ===============================================================================

// Simple user space program that prints hello world
static void user_program_entry(void *arg)
{
    (void)arg;
    
    // This would normally be in user space
    // For now, we'll simulate it from kernel space
    print_colored("🎉 USER SPACE PROCESS STARTED!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("Hello from user space process!\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("PID: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    
    // Get current process info
    process_t *current = process_get_current();
    if (current) {
        print_uint32(current->pid);
        print("\n");
    }
    
    print_colored("User process running successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Simulate some work
    for (int i = 0; i < 5; i++) {
        print_colored("User process iteration: ", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
        print_uint32(i + 1);
        print("\n");
        
        // Small delay
        for (volatile int j = 0; j < 1000000; j++) {
            __asm__ volatile("nop");
        }
    }
    
    print_colored("User process completed successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Exit the process
    process_exit(0);
}

// Create and start the first user space process
static void user_start(void)
{
    print_colored("🚀 Starting first user space process...\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
    
    // Create the user process
    process_t *user_process = process_create("user_program", user_program_entry, NULL);
    if (!user_process) {
        print_error("[ERROR] Failed to create user process\n");
        return;
    }
    
    print_colored("✅ User process created successfully!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("Process PID: ", VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
    print_uint32(user_process->pid);
    print("\n");
    
    // Add the process to the scheduler
    task_t *user_task = create_task(user_program_entry, NULL, 5, 0);
    if (!user_task) {
        print_error("[ERROR] Failed to create user task\n");
        return;
    }
    
    // Add to scheduler
    add_task(user_task);
    
    print_colored("✅ User process added to scheduler!\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    print_colored("🎯 User space transition ready!\n", VGA_COLOR_MAGENTA, VGA_COLOR_BLACK);
}

// Main kernel entry point (called from arch_x64.c)
void main(void)
{
    // Mark kernel as running
    kernel_running = true;

    // Initialize kernel in order
    early_init();
    memory_init();

    // Enable interrupts
    enable_interrupts();

    print_success("[OK] Kernel initialization completed successfully\n");

    // Delay for visual effect
    delay_ms(1500);

    // SIMPLIFICADO: Solo pruebas básicas del heap
    print_success("[OK] Testing basic heap functionality...\n");
    delay_ms(1000);

    // Test básico: kmalloc simple
    void *ptr = kmalloc(64);
    if (ptr)
    {
        print_success("[OK] Basic kmalloc(64) successful\n");
        kfree(ptr);
        print_success("[OK] Basic kfree() successful\n");
    }
    else
    {
        print_error("[ERROR] Basic kmalloc failed\n");
    }

    delay_ms(1000);

    // Main kernel loop
    print_success("[OK] Kernel boot completed successfully\n");
    
    delay_ms(1000);

    // AUTHENTICATION SYSTEM
    print_success("[OK] Initializing authentication system...\n");
    init_auth_system();
    
    // Kernel login - required before shell access
    auth_result_t login_result = kernel_login();
    if (login_result != AUTH_SUCCESS) {
        // Login failed - system halted in kernel_login()
        return;
    }

    // SHELL INTERACTIVO MEJORADO
    print_success("==========================================\n");
    print_success("[OK] Starting interactive shell...\n");
    print_success("==========================================\n");

    // Iniciar shell interactivo
    shell_start();

    // Cuando el shell termina, crear el primer proceso de user space
    print_success("[OK] Shell exited, creating first user space process...\n");
    print_success("==========================================\n");
    print_success("IR0 Kernel - User Space Transition\n");
    print_success("==========================================\n");

    // Crear y iniciar el primer proceso de user space
    user_start();

    // Iniciar el scheduler
    scheduler_start();
    
    // Ir al dispatch loop del scheduler (nunca retorna)
    scheduler_dispatch_loop();
}