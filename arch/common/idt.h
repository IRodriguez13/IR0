#pragma once // Evita tener que hacer ifdef, endif pq esta abstraído
#include <stdint.h>

#define IDT_ENTRIES 256 // cantidad de entradas de nuestro idt
#define IDT_INTERRUPT_GATE_KERNEL  0x8E  // DPL=0, solo kernel
#define IDT_INTERRUPT_GATE_USER    0xEE  // DPL=3, user puede usar
#define IDT_TRAP_GATE_KERNEL       0x8F  // trap gate, kernel
#define IDT_FLAG_GATE32   0x0E // 
#define IDT_FLAG_TRAP32   0x0F // es para pruebas 


#if defined(__x86_64__)
    #include "../x_64/sources/idt_arch_x64.h"
#elif defined(__i386__) // 32 bit por si no la cazaste
    #include "../x86-32/sources/idt_arch_x86.h"
#else
    #error "Arquitectura no soportada"
#endif

// Funciones comunes
void idt_init();
void idt_set_gate(int n, uintptr_t handler, uint8_t flags);


