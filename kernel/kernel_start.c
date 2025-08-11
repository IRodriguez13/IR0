// kernel/kernel_start.c - ACTUALIZADO CON RUTAS CORRECTAS
#include <ir0/print.h>

// ARREGLADO: Includes con rutas correctas según arquitectura
#if defined(__i386__)
    #include "../arch/x86-32/sources/Paging_x86.h"
    #define init_paging() init_paging_x86()
#elif defined(__x86_64__)  
    #include "../arch/x_64/sources/Paging_x64.h"  // ME FALTA PAGINACION DE 64 BIT
    #define init_paging() init_paging_x64()
#else
    #error "Arquitectura no soportada en kernel_start.c"
#endif

#include <idt.h>
#include <panic/panic.h>
#include "../drivers/timer/clock_system.h"
#include "scheduler/scheduler.h"
#include "scheduler/task.h"
#include "../arch/common/arch_interface.h"  

void main()
{
    clear_screen();
    print_colored("=== IR0 KERNEL BOOT === :-)\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Inicialización básica del sistema
    idt_init();
    LOG_OK("IDT cargado correctamente");

    init_paging();
    LOG_OK("Paginación inicializada");

    // Inicializar scheduler
    scheduler_init();
    LOG_OK("Scheduler inicializado");

    // NUEVO: Crear procesos de prueba ANTES de iniciar scheduler
    create_dummy_tasks();
    LOG_OK("Procesos dummy creados");

    // Opcional: Mostrar estado del scheduler
    dump_scheduler_state();

    // Inicializar sistema de timers (esto va a llamar scheduler_tick)
    init_clock();
    LOG_OK("Sistema de timers inicializado");

    // Activar interrupciones
    arch_enable_interrupts();  
    LOG_OK("Interrupciones habilitadas");

    print_colored("=== STARTING SCHEDULER ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);

    // scheduler_start(); porque no tengo procesos hasta tanto no tenga lo que me falta para ejecutar programas.

    // No debería llegar aquí nunca
    panic("Scheduler returned unexpectedly!");
}

void ShutDown() 
{
    print_warning("System shutdown requested\n");
    
    // Intentar apagar via ACPI primero
    outb(0x604, 0x2000);  // QEMU/ACPI shutdown
    
    // Si no funciona, reset via keyboard controller
    uint8_t reset_value = 0xFE;
    asm volatile(
        "outb %%al, $0x64"
        :                  
        : "a"(reset_value) 
        : "memory"         
    );
    
    // Último recurso: hang
    print_error("Shutdown failed, hanging CPU\n");
    cpu_relax();
}