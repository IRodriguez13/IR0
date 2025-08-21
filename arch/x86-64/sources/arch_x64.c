// arch/x86-64/sources/arch_x64.c - SIMPLIFICADO
#include <kernel_start.h>
#include <arch_interface.h>
#include <ir0/print.h>
#include "arch_x64.h"
#include <ir0/panic/panic.h>

// Implementaciones de funciones de I/O
void outb(uint16_t port, uint8_t value) 
{
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) 
{
    uint8_t value;

    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    
    return value;
}

void cpu_wait(void) 
{
    __asm__ volatile("hlt");
}

// Implementaciones de funciones de I/O
void outb(uint16_t port, uint8_t value) {
    __asm__ volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

uint8_t inb(uint16_t port) {
    uint8_t value;
    __asm__ volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

void kmain_x64(void)
{
    // Setup mínimo específico de x86-64
    __asm__ volatile("cli"); // Deshabilitar interrupciones al arrancar
    
    // Log de entrada
    print_colored("=== IR0 Kernel x86-64 ===\n", VGA_COLOR_GREEN, VGA_COLOR_BLACK);
    
    // Saltar a la función principal
    main();
    
    // Si llegamos aquí, algo salió mal
    panic("kmain_x64: main() retornó - esto no debería pasar!\n");
}
