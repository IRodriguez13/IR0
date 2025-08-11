#include "../../kernel/kernel_start.h"
#include "../../common/arch_interface.h"


// Esta es la función que llama el boot.asm
void kmain_x64(void) 
{
    // Setup mínimo específico de x86-64 ANTES del kernel principal
    __asm__ volatile("cli");  // Deshabilitar interrupciones al arrancar.
    
    // Saltamos a la función principal una vez terminado el inicio.
    main();
}


// Implementación de la interfaz común
void arch_enable_interrupts(void) 
{
    __asm__ volatile("sti");
}


const char* arch_get_name(void) 
{
    return "x64";
}
