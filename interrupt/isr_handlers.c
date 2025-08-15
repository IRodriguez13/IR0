// interrupt/isr_handlers.c - ARREGLADO
#include <print.h>
#include <panic/panic.h>
#include <stdint.h>
#include <arch_interface.h>
#include "isr_handlers.h"
#include "../arch/common/idt.h"
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

    // Por ahora, hacer el timer handler muy simple para evitar crashes
    // Solo enviar EOI y continuar
    
    // EOI simple - siempre enviar al PIC por ahora
    outb(0x20, 0x20); // EOI al PIC
    
    // TODO: Implementar scheduler_tick() cuando el scheduler esté completamente estable
    // TODO: Implementar EOI específico según el timer usado
}