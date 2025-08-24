// arch/x86-64/sources/arch_x64.c - SIMPLIFICADO
#include <kernel_start.h>
#include <arch_interface.h>
#include <ir0/print.h>
#include "arch_x64.h"
#include <ir0/panic/panic.h>

// I/O functions are now in arch_interface.h

void kmain_x64(void)
{
    // Setup mínimo específico de x86-64
    __asm__ volatile("cli"); // Deshabilitar interrupciones al arrancar
        
    // Saltar a la función principal
    main();
    
    // Si llegamos aquí, algo salió mal
    panic("kmain_x64: main() retornó - esto no debería pasar!\n");
}
