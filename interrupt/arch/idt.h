#ifndef IDT_H
#define IDT_H

#include <stdint.h>

// Estructura de entrada IDT para 32-bit
struct idt_entry32 {
    uint16_t base_lo;
    uint16_t sel;
    uint8_t always0;
    uint8_t flags;
    uint16_t base_hi;
} __attribute__((packed));

// Estructura de entrada IDT para 64-bit
struct idt_entry64 {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t flags;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t reserved;
} __attribute__((packed));

// Puntero IDT para 32-bit
struct idt_ptr32 {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// Puntero IDT para 64-bit
struct idt_ptr64 {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

// Declaraciones de funciones
void idt_init32(void);
void idt_init64(void);
void idt_load32(void);
void idt_load64(void);

// Handlers de interrupciones
void isr_handler32(uint32_t interrupt_number);
void isr_handler64(uint64_t interrupt_number);

// Funciones PIC
void pic_remap32(void);
void pic_remap64(void);
void pic_send_eoi32(uint8_t irq);
void pic_send_eoi64(uint8_t irq);

// Funciones de teclado
void keyboard_handler32(void);
void keyboard_handler64(void);
void keyboard_init(void);

// Funciones del buffer de teclado
char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);

// Funciones del nuevo driver de teclado
void keyboard_handler64(void);
void keyboard_init(void);

// Declaraciones de stubs de interrupción
#ifdef __x86_64__
// Stubs para 64-bit
extern void isr0_64(void);
extern void isr1_64(void);
extern void isr2_64(void);
extern void isr3_64(void);
extern void isr4_64(void);
extern void isr5_64(void);
extern void isr6_64(void);
extern void isr7_64(void);
extern void isr8_64(void);
extern void isr9_64(void);
extern void isr10_64(void);
extern void isr11_64(void);
extern void isr12_64(void);
extern void isr13_64(void);
extern void isr14_64(void);
extern void isr15_64(void);
extern void isr16_64(void);
extern void isr17_64(void);
extern void isr18_64(void);
extern void isr19_64(void);
extern void isr20_64(void);
extern void isr21_64(void);
extern void isr22_64(void);
extern void isr23_64(void);
extern void isr24_64(void);
extern void isr25_64(void);
extern void isr26_64(void);
extern void isr27_64(void);
extern void isr28_64(void);
extern void isr29_64(void);
extern void isr30_64(void);
extern void isr31_64(void);
extern void isr32_64(void);
extern void isr33_64(void);
extern void isr34_64(void);
extern void isr35_64(void);
extern void isr36_64(void);
extern void isr37_64(void);
extern void isr38_64(void);
extern void isr39_64(void);
extern void isr40_64(void);
extern void isr41_64(void);
extern void isr42_64(void);
extern void isr43_64(void);
extern void isr44_64(void);
extern void isr45_64(void);
extern void isr46_64(void);
extern void isr47_64(void);
#else
// Stubs para 32-bit
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
#endif

// Variables globales
#ifdef __x86_64__
extern struct idt_entry64 idt[256];
extern struct idt_ptr64 idt_ptr;
#else
extern struct idt_entry32 idt[256];
extern struct idt_ptr32 idt_ptr;
#endif

#endif // IDT_H
