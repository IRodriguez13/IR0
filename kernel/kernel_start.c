#include <ir0/print.h>
#include "../paging/Paging.h"
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

    idt_init();
    LOG_OK("IDT cargado correctamente");

    init_paging();
    LOG_OK("Paginación inicializada");

    scheduler_init();
    LOG_OK("Scheduler inicializado");

    // Activar interrupciones
    arch_enable_interrupts();  
    LOG_OK("Interrupciones habilitadas");

    // Iniciar scheduler (va a saltar al primer proceso)
    scheduler_start();

    // No debería llegar aquí
    panic("Scheduler returned unexpectedly!");
}

void ShutDown() // Como no tengo drivers para apagar la máquina, no la puedo usar.
{
    uint8_t reset_value = 0xFE;
    asm volatile(
        "outb %%al, $0x64"
        :                  // outputs (ninguno)
        : "a"(reset_value) // inputs: reset_value en AL
        : "memory"         // clobbers: indica que puede modificar memoria
    );
}
