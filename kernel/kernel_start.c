#include "kernel_start.h"
#include <print.h>
#include <panic/panic.h>
#include <logging.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
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
#include "../drivers/IO/ps2.h"
#include "../drivers/storage/ata.h"
#include "../interrupt/arch/idt.h"
#include "../interrupt/arch/pic.h"
#ifdef __x86_64__
#include "../arch/x86-64/sources/tss_x64.h"
#endif
#ifdef __x86_64__
#include "../memory/arch/x86-64/Paging_x64.h"
#else
#include "../memory/arch/x_86-32/Paging_x86-32.h"
#endif

void main(void)
{
    // Banner de inicio
    print_colored("╔══════════════════════════════════════════════════════════════╗\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                    IR0 Kernel v0.0.0                         ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("║                                                              ║\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    print_colored("╚══════════════════════════════════════════════════════════════╝\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    delay_ms(1000);

    // 0. Inicializar sistema de logging
    logging_init();
    logging_set_level(LOG_LEVEL_INFO);
    log_info("KERNEL", "System initialization started");

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

    // 2. Inicializar paginación
    log_info("KERNEL", "Initializing memory management");
#ifdef __x86_64__
    init_paging_x64();

    // Verificar que el kernel esté mapeado correctamente
    extern int paging_verify_mapping(uint64_t virt_addr);

    // Verificar direcciones clave del kernel
    uint64_t kernel_start = 0x100000; // Donde se carga el kernel
    uint64_t kernel_end = 0x200000;   // Aproximadamente 1MB después

    int mapping_ok = 1;
    for (uint64_t addr = kernel_start; addr < kernel_end; addr += 0x1000)
    {
        if (!paging_verify_mapping(addr))
        {
            log_error_fmt("KERNEL", "Memory mapping verification failed at 0x%llx", addr);
            mapping_ok = 0;
            break;
        }
    }

    if (mapping_ok)
    {
        log_info("KERNEL", "Memory mapping verified successfully");
    }
    else
    {
        log_fatal("KERNEL", "Memory mapping verification failed");
        panic("Memory mapping verification failed");
    }
    
    // Initialize TSS for x86-64 (needed for proper interrupt handling)
    tss_init_x64();
    log_info("KERNEL", "TSS initialized");
#else
    init_paging_x86();
#endif
    log_info("KERNEL", "Memory management initialized");
    delay_ms(1500);

    // 3. Inicializar allocators
    log_info("KERNEL", "Initializing memory allocators");
    heap_allocator_init();
    physical_allocator_init();
    log_info("KERNEL", "Memory allocators initialized");
    delay_ms(1500);

    // 4. Inicializar scheduler
    log_info("KERNEL", "Initializing task scheduler");
    scheduler_init();
    log_info("KERNEL", "Task scheduler initialized");
    delay_ms(1500);

    // 4.1. Inicializar sistema de procesos
    log_info("KERNEL", "Initializing process management");
    process_init();
    log_info("KERNEL", "Process management initialized");
    delay_ms(1500);

    // 4.2. Inicializar sistema de system calls
    log_info("KERNEL", "Initializing system call interface");
    syscalls_init();
    log_info("KERNEL", "System call interface initialized");
    delay_ms(1500);

    // 5. Inicializar timer system
    log_info("KERNEL", "Initializing timer system");
    init_clock();
    log_info("KERNEL", "Timer system initialized");
    delay_ms(1500);

    // 6. Inicializar drivers de hardware
    log_info("KERNEL", "Initializing hardware drivers");

    // Inicializar driver de teclado personalizado
    keyboard_init();
    log_info("KERNEL", "Keyboard driver initialized");

    // Inicializar driver de disco ATA
    ata_init();
    log_info("KERNEL", "ATA disk driver initialized");

    delay_ms(1500);

    // 7. Inicializar VFS
    log_info("KERNEL", "Initializing virtual file system");
    vfs_init();
    log_info("KERNEL", "Virtual file system initialized");
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
    delay_ms(2500);

    log_info("KERNEL", "Starting command shell");

    // Initialize shell
    shell_context_t shell_ctx;
    shell_config_t shell_config;

    if (shell_init(&shell_ctx, &shell_config) != 0)
    {
        log_fatal("KERNEL", "Shell initialization failed");
        panic("Shell initialization failed");
    }

    // Run shell (this will return when shell is done)
    int shell_result = shell_run(&shell_ctx, &shell_config);

    if (shell_result == 0)
    {
        log_info("KERNEL", "Shell completed successfully");
        log_info("KERNEL", "Starting scheduler main loop");

        // Iniciar scheduler y entrar al loop principal
        scheduler_start();
        scheduler_main_loop(); // NUNCA RETORNA
    }
    else
    {
        log_error_fmt("KERNEL", "Shell failed with error code: %d", shell_result);
        log_info("KERNEL", "Continuing without shell");

        // Continue kernel operation even if shell fails
        log_info("KERNEL", "Starting scheduler main loop");
        scheduler_start();
        scheduler_main_loop(); // NUNCA RETORNA
    }
}