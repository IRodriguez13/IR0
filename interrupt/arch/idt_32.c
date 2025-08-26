#include "idt32.h"
#include <stdint.h>

// Variables globales
struct idt_entry32 idt[256];
struct idt_ptr32   idt_ptr;

// Función para configurar una entrada de la IDT
void idt_set_gate32(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags)
{
    idt[num].offset_low  = base & 0xFFFF;
    idt[num].selector    = sel;
    idt[num].zero        = 0;
    idt[num].flags       = flags;
    idt[num].offset_high = (base >> 16) & 0xFFFF;
}

// Inicialización de la IDT
void idt_init32(void)
{
    idt_ptr.limit = sizeof(struct idt_entry32) * 256 - 1;
    idt_ptr.base  = (uint32_t)&idt;

    // Inicializar todas las entradas a 0
    for (int i = 0; i < 256; i++) 
    {
        idt_set_gate32(i, 0, 0, 0);
    }

    // Excepciones CPU (ISRs 0–31)
    idt_set_gate32(0,  (uint32_t)isr0_32,  0x08, 0x8E);
    idt_set_gate32(1,  (uint32_t)isr1_32,  0x08, 0x8E);
    idt_set_gate32(2,  (uint32_t)isr2_32,  0x08, 0x8E);
    idt_set_gate32(3,  (uint32_t)isr3_32,  0x08, 0x8E);
    idt_set_gate32(4,  (uint32_t)isr4_32,  0x08, 0x8E);
    idt_set_gate32(5,  (uint32_t)isr5_32,  0x08, 0x8E);
    idt_set_gate32(6,  (uint32_t)isr6_32,  0x08, 0x8E);
    idt_set_gate32(7,  (uint32_t)isr7_32,  0x08, 0x8E);
    idt_set_gate32(8,  (uint32_t)isr8_32,  0x08, 0x8E);
    idt_set_gate32(9,  (uint32_t)isr9_32,  0x08, 0x8E);
    idt_set_gate32(10, (uint32_t)isr10_32, 0x08, 0x8E);
    idt_set_gate32(11, (uint32_t)isr11_32, 0x08, 0x8E);
    idt_set_gate32(12, (uint32_t)isr12_32, 0x08, 0x8E);
    idt_set_gate32(13, (uint32_t)isr13_32, 0x08, 0x8E);
    idt_set_gate32(14, (uint32_t)isr14_32, 0x08, 0x8E);
    idt_set_gate32(15, (uint32_t)isr15_32, 0x08, 0x8E);
    idt_set_gate32(16, (uint32_t)isr16_32, 0x08, 0x8E);
    idt_set_gate32(17, (uint32_t)isr17_32, 0x08, 0x8E);
    idt_set_gate32(18, (uint32_t)isr18_32, 0x08, 0x8E);
    idt_set_gate32(19, (uint32_t)isr19_32, 0x08, 0x8E);
    idt_set_gate32(20, (uint32_t)isr20_32, 0x08, 0x8E);
    idt_set_gate32(21, (uint32_t)isr21_32, 0x08, 0x8E);
    idt_set_gate32(22, (uint32_t)isr22_32, 0x08, 0x8E);
    idt_set_gate32(23, (uint32_t)isr23_32, 0x08, 0x8E);
    idt_set_gate32(24, (uint32_t)isr24_32, 0x08, 0x8E);
    idt_set_gate32(25, (uint32_t)isr25_32, 0x08, 0x8E);
    idt_set_gate32(26, (uint32_t)isr26_32, 0x08, 0x8E);
    idt_set_gate32(27, (uint32_t)isr27_32, 0x08, 0x8E);
    idt_set_gate32(28, (uint32_t)isr28_32, 0x08, 0x8E);
    idt_set_gate32(29, (uint32_t)isr29_32, 0x08, 0x8E);
    idt_set_gate32(30, (uint32_t)isr30_32, 0x08, 0x8E);
    idt_set_gate32(31, (uint32_t)isr31_32, 0x08, 0x8E);

    // IRQs PIC remapeadas 32–47
    idt_set_gate32(32, (uint32_t)isr32_32, 0x08, 0x8E);
    idt_set_gate32(33, (uint32_t)isr33_32, 0x08, 0x8E);
    idt_set_gate32(34, (uint32_t)isr34_32, 0x08, 0x8E);
    idt_set_gate32(35, (uint32_t)isr35_32, 0x08, 0x8E);
    idt_set_gate32(36, (uint32_t)isr36_32, 0x08, 0x8E);
    idt_set_gate32(37, (uint32_t)isr37_32, 0x08, 0x8E);
    idt_set_gate32(38, (uint32_t)isr38_32, 0x08, 0x8E);
    idt_set_gate32(39, (uint32_t)isr39_32, 0x08, 0x8E);
    idt_set_gate32(40, (uint32_t)isr40_32, 0x08, 0x8E);
    idt_set_gate32(41, (uint32_t)isr41_32, 0x08, 0x8E);
    idt_set_gate32(42, (uint32_t)isr42_32, 0x08, 0x8E);
    idt_set_gate32(43, (uint32_t)isr43_32, 0x08, 0x8E);
    idt_set_gate32(44, (uint32_t)isr44_32, 0x08, 0x8E);
    idt_set_gate32(45, (uint32_t)isr45_32, 0x08, 0x8E);
    idt_set_gate32(46, (uint32_t)isr46_32, 0x08, 0x8E);
    idt_set_gate32(47, (uint32_t)isr47_32, 0x08, 0x8E);

    // Se pueden agregar más ISRs personalizadas si se necesitan
}
