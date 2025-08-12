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

// Implementación de la interfaz común
void arch_enable_interrupts(void) 
{
    __asm__ volatile("sti");
}

const char* arch_get_name(void) 
{
    return "x86-32 (i386)";
}

uintptr_t read_fault_address() 
{
    uintptr_t addr;
    // En x86-32, CR2 también contiene la dirección del fault
    asm volatile("mov %%cr2, %0" : "=r"(addr));
    return addr;
}

// NUEVO: Implementar outb para 32-bit (movido desde archivo separado)
void outb(uint16_t port, uint8_t value)
{
    asm volatile("outb %0, %1" :: "a"(value), "Nd"(port));
}
