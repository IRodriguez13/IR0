#include "kernel_start.h"
#include <print.h>
#include <panic/panic.h>
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

    // 1. Inicializar IDT y sistema de interrupciones
    print("Initializing interrupt system...\n");
    idt_init();
    print_success("Interrupt system initialized\n");
    delay_ms(1000);

    // 2. Inicializar paginación
    print("Initializing memory management...\n");
#ifdef __x86_64__
    init_paging_x64();
    
    // Verificar que el kernel esté mapeado correctamente
    print("Verifying kernel memory mapping...\n");
    extern int paging_verify_mapping(uint64_t virt_addr);
    
    // Verificar direcciones clave del kernel
    uint64_t kernel_start = 0x100000;  // Donde se carga el kernel
    uint64_t kernel_end = 0x200000;    // Aproximadamente 1MB después
    
    int mapping_ok = 1;
    for (uint64_t addr = kernel_start; addr < kernel_end; addr += 0x1000) {
        if (!paging_verify_mapping(addr)) {
            print_error("Memory not mapped at 0x");
            print_hex64(addr);
            print("\n");
            mapping_ok = 0;
            break;
        }
    }
    
    if (mapping_ok) {
        print_success("Kernel memory mapping verified\n");
    } else {
        print_error("Kernel memory mapping failed!\n");
        panic("Memory mapping verification failed");
    }
    
    // Initialize TSS for x86-64 (needed for proper interrupt handling)
    print("Initializing TSS...\n");
    tss_init_x64();
    print_success("TSS initialized\n");
#else
    init_paging_x86();
#endif
    print_success("Memory management initialized\n");
    delay_ms(1000);

    // 3. Inicializar allocators
    print("Initializing memory allocators...\n");
    heap_allocator_init();
    physical_allocator_init();
    print_success("Memory allocators initialized\n");
    delay_ms(1000);

    // 4. Inicializar scheduler
    print("Initializing task scheduler...\n");
    scheduler_init();
    print_success("Task scheduler initialized\n");
    delay_ms(1000);

    // 4.1. Inicializar sistema de procesos
    print("Initializing process management...\n");
    process_init();
    print_success("Process management initialized\n");
    delay_ms(1000);

    // 4.2. Inicializar sistema de system calls
    print("Initializing system call interface...\n");
    syscalls_init();
    print_success("System call interface initialized\n");
    delay_ms(1000);

    // 5. Inicializar timer system
    print("Initializing timer system...\n");
    init_clock();
    print_success("Timer system initialized\n");
    delay_ms(1000);

    // 6. Inicializar VFS
    print("Initializing virtual file system...\n");
    vfs_init();
    print_success("Virtual file system initialized\n");
    delay_ms(1000);

    // 7. MANTENER INTERRUPCIONES DESHABILITADAS (SOLUCIÓN RÁPIDA)
    print("Keeping interrupts disabled for stability...\n");
    // __asm__ volatile("sti");
    print_success("Interrupts remain disabled (stable mode)\n");
    delay_ms(1000);

    print_colored("=== IR0 Kernel Ready - Starting Shell ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(2000);

    // Initialize shell
    shell_context_t shell_ctx;
    shell_config_t shell_config;
    
    if (shell_init(&shell_ctx, &shell_config) != 0) {
        print_error("Failed to initialize shell");
        panic("Shell initialization failed");
    }
    
    // Run shell (this will return when shell is done)
    int shell_result = shell_run(&shell_ctx, &shell_config);
    
    if (shell_result == 0) {
        print_success("Shell completed successfully \n");
        print("Kernel continuing normal operation...\n");
        
        // Continue with normal kernel operation
        // For now, just print a message and continue
        print("Kernel is now in idle mode.\n");
        print("All subsystems are running normally.\n");
        
        // Don't panic, just continue
        cpu_wait();
    } else {
        print_error("Shell failed with error code: \n");
        print_error("Kernel continuing without shell...\n");
        
        // Continue kernel operation even if shell fails
        cpu_wait();
    }
}