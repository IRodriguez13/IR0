#include "kernel_start.h"
#include "../includes/ir0/kernel.h"
#include "../includes/ir0/print.h"
#include "../includes/ir0/panic/panic.h"
#include "../memory/memo_interface.h"
#include "../memory/physical_allocator.h"
#include "../memory/heap_allocator.h"
#include "../memory/krnl_memo_layout.h"
#include "../kernel/scheduler/scheduler.h"
#include "../drivers/timer/clock_system.h"
#include "../fs/vfs.h"
#include <string.h>

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
    init_paging_x64();
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
    
    // 7. Habilitar interrupciones
    print("Enabling interrupts...\n");
    __asm__ volatile("sti");
    print_success("Interrupts enabled\n");
    delay_ms(1000);
    
    print_colored("=== IR0 Kernel Ready - Starting Scheduler ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    delay_ms(2000);
    
    // Iniciar el scheduler (esto entrará en el dispatch loop)
    scheduler_start();
    
    // Si llegamos aquí, algo salió mal
    panic("main: scheduler_start() retornó - esto no debería pasar!\n");
}