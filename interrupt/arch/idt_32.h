#pragma once
#include <stdint.h>

// Estructura de entrada IDT para 32-bit
struct idt_entry32 
{
    uint16_t offset_low;    // Bits 0-15 del offset
    uint16_t selector;      // Selector del segmento
    uint8_t zero;           // Siempre 0 en 32-bit
    uint8_t flags;          // Tipo y privilegios
    uint16_t offset_high;   // Bits 16-31 del offset
} __attribute__((packed));

// Puntero IDT para 32-bit
struct idt_ptr32 
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Declaraciones de funciones específicas para 32-bit
void idt_init32(void);
void idt_load32(void);
void idt_set_gate32(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags);

// Handler de interrupciones para 32-bit
void isr_handler32(uint32_t interrupt_number);

// Funciones PIC para 32-bit
void pic_remap32(void);
void pic_send_eoi32(uint8_t irq);

// Funciones de teclado para 32-bit
void keyboard_handler32(void);

// Declaraciones de stubs de interrupción para 32-bit
extern void isr0_32(void);
extern void isr1_32(void);
extern void isr2_32(void);
extern void isr3_32(void);
extern void isr4_32(void);
extern void isr5_32(void);
extern void isr6_32(void);
extern void isr7_32(void);
extern void isr8_32(void);
extern void isr9_32(void);
extern void isr10_32(void);
extern void isr11_32(void);
extern void isr12_32(void);
extern void isr13_32(void);
extern void isr14_32(void);
extern void isr15_32(void);
extern void isr16_32(void);
extern void isr17_32(void);
extern void isr18_32(void);
extern void isr19_32(void);
extern void isr20_32(void);
extern void isr21_32(void);
extern void isr22_32(void);
extern void isr23_32(void);
extern void isr24_32(void);
extern void isr25_32(void);
extern void isr26_32(void);
extern void isr27_32(void);
extern void isr28_32(void);
extern void isr29_32(void);
extern void isr30_32(void);
extern void isr31_32(void);
extern void isr32_32(void);
extern void isr33_32(void);
extern void isr34_32(void);
extern void isr35_32(void);
extern void isr36_32(void);
extern void isr37_32(void);
extern void isr38_32(void);
extern void isr39_32(void);
extern void isr40_32(void);
extern void isr41_32(void);
extern void isr42_32(void);
extern void isr43_32(void);
extern void isr44_32(void);
extern void isr45_32(void);
extern void isr46_32(void);
extern void isr47_32(void);

// Variables globales para 32-bit
extern struct idt_entry32 idt[256];
extern struct idt_ptr32 idt_ptr;

