// interrupt/isr_handlers.c - ARREGLADO
#include <print.h>
#include <panic/panic.h>
#include <stdint.h>
#include <arch_interface.h>
#include "isr_handlers.h"
#include "../drivers/timer/clock_system.h"
#include "../memory/ondemand-paging.h"


void default_interrupt_handler()
{
    print_warning("\n[ISR] Interrupción no manejada\n");
    // No hacer panic inmediatamente, solo loggear ....
    print_warning("Continuando ejecución...\n");
}

void page_fault_handler()
{
    print_error("\n[ISR] *** PAGE FAULT DETECTADO! ***\n");

    uintptr_t fault_addr = read_fault_address();

    print_error("Dirección que causó el fallo: ");
    print_hex_compact(fault_addr);
    print("\n");

    page_fault_handler_improved(); 

    print_error("Razón: Acceso a memoria no mapeada o sin permisos\n");

    // Por ahora, hacer panic. En el futuro implementar lazy allocation cuando tenga manejo de memo.
    panic("Page fault: abortando ejecución");
}

// MANTENER SOLO EN: interrupt/isr_handlers.c
// En interrupt/isr_handlers.c, completar el switch:
void time_handler()
{
    static uint32_t tick_count = 0;
    tick_count++;

    // Llamar scheduler si está inicializado
    if (scheduler_ready())
    {
        scheduler_tick();
    }

    // EOI según timer usado
    enum ClockType timer = get_current_timer_type();
    switch (timer)
    {
    case CLOCK_HPET:
        // HPET maneja sus propios EOI
        break;
    case CLOCK_LAPIC:
        lapic_send_eoi();
        break;
    case CLOCK_PIT:
    case CLOCK_RTC:
        outb(0x20, 0x20); // EOI al PIC
        break;
    case CLOCK_NONE: 
    default:
        outb(0x20, 0x20); // EOI por defecto
        break;
    }
}