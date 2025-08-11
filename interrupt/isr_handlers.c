// interrupt/isr_handlers.c - ARREGLADO
#include <print.h>
#include <panic/panic.h>
#include <stdint.h>
#include <arch_interface.h>  
#include "isr_handlers.h"

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
    
    print_error("Razón: Acceso a memoria no mapeada o sin permisos\n");
    
    // Por ahora, hacer panic. En el futuro implementar lazy allocation cuando tenga manejo de memo.
    panic("Page fault: abortando ejecución");
}
