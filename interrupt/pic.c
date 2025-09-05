#include "pic.h"
#include <ir0/print.h>
#include <arch/common/arch_interface.h>

// I/O functions
// Using I/O functions from arch_interface.h

// Initialize PIC - VECTORES ESTÁNDAR (32-47)
void pic_init(void)
{
    unsigned char a1, a2;

    // Save masks
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    // Start initialization sequence (cascade mode)
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);

    // ICW2: Master PIC vector offset - VECTORES ESTÁNDAR
    outb(PIC1_DATA, 32); // IRQ 0-7: interrupts 32-39
    outb(PIC2_DATA, 40); // IRQ 8-15: interrupts 40-47

    // ICW3: tell Master PIC that there is a slave PIC at IRQ2 (0000 0100)
    outb(PIC1_DATA, 4);
    // ICW3: tell Slave PIC its cascade identity (0000 0010)
    outb(PIC2_DATA, 2);

    // ICW4: have the PICs use 8086 mode (and not 8080 mode)
    outb(PIC1_DATA, ICW4_8086);
    outb(PIC2_DATA, ICW4_8086);

    // MASK ALL IRQs initially for safety
    outb(PIC1_DATA, 0xFF);
    outb(PIC2_DATA, 0xFF);

    // PIC initialized with standard vector mapping (32-47)
}

// Send End of Interrupt signal
void pic_send_eoi(unsigned char irq)
{
    if (irq >= 8)
    {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

// Mask IRQ
void pic_mask_irq(unsigned char irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}

// Unmask IRQ
void pic_unmask_irq(unsigned char irq)
{
    uint16_t port;
    uint8_t value;

    if (irq < 8)
    {
        port = PIC1_DATA;
    }
    else
    {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
