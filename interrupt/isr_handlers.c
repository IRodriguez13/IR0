// interrupt/isr_handlers.c - IMPLEMENTACIÓN MÍNIMA
#include "isr_handlers.h"
#include "../includes/ir0/print.h"

// Handler mínimo ya definido en idt.c
// void isr_handler(uint8_t int_no) está en idt.c

// Función simple para enviar EOI
void isr_send_eoi(uint8_t irq) {
    if (irq >= 8) {
        // Send EOI to slave PIC
        __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0xA0));
    }
    // Send EOI to master PIC
    __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0x20));
}

// Handler de timer simple
void time_handler(void) {
    // Solo enviar EOI
    isr_send_eoi(0);
}