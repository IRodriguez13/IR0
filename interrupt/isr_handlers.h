// interrupt/isr_handlers.h - IMPLEMENTACIÓN MÍNIMA
#pragma once

#include <stdint.h>

// Handler principal de interrupciones (definido en idt.c)
void isr_handler(uint8_t int_no);

// Función para enviar EOI
void isr_send_eoi(uint8_t irq);

// Handler de timer simple
void time_handler(void); 