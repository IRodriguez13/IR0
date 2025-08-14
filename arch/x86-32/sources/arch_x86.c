// arch/x86-32/sources/arch.c - ACTUALIZADO
#include "../../kernel/kernel_start.h"
#include "../../common/arch_interface.h"

// Esta es la función que llama el boot.asm
void kmain_x32(void) 
{
    // Setup mínimo específico de x86-32 ANTES del kernel principal
    __asm__ volatile("cli");  // Deshabilitar interrupciones al arrancar.
    
    // Saltamos a la función principal una vez terminado el inicio.
    main();
}

