#pragma once
#include <stdint.h>


void idt_arch_set_gate(int n, uintptr_t handler, uint8_t flags);


typedef struct 
{
    uint16_t offset_low;   // Bits 0-15 del offset
    uint16_t selector;     // Segment selector (GDT)
    uint8_t  zero;         // Siempre 0 en x86
    uint8_t  type_attr;    // P(1) | DPL(2) | 0(1) | GateType(4)
    uint16_t offset_high;  // Bits 16-31 del offset
} __attribute__((packed)) idt_entry_t;

// Estructura del puntero IDT para 'lidt'
typedef struct {
    uint16_t limit;
    uint32_t base;         // Dirección lineal de la IDT
} __attribute__((packed)) idt_ptr_t;

// 32 bit

// Funciones ASM específicas de arquitectura
extern void idt_flush(uintptr_t);
extern void isr_default(void);
extern void isr_page_fault(void);
extern void timer_stub(void);

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 32 bit --
void paging_set_cpu();

