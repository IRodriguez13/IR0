#include <print.h>
#include <panic/panic.h>
#include <stdint.h>
#include "isr_handlers.h"

void default_interrupt_handler()
{
    print("\n[ISR] Interrupción no manejada\n");
    panic("Se invocó el default_interrupt_handler");
}

void page_fault_handler()
{
    uintptr_t fault_addr;

    // Leer CR2 para saber qué dirección causó el page fault
    uintptr_t read_fault_address();
    print("\n[ISR] Page Fault detectado!\n");
    


    // La idea ahora es fijarme si conviene pasar a un lazy allocation para poder manejar on demand el paging.
    // Opcional: más diagnóstico en el futuro (flags de error, etc.)
    panic("Page fault: abortando ejecución");
}


