#include "pic.h"
#include "io.h"
#include <ir0/print.h>

// Remapear PIC para 32-bit
void pic_remap32(void) {
    uint8_t a1, a2;
    (void)a1; (void)a2; // Variables not used in this implementation

    // Guardar máscaras originales
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    // Inicializar PIC1
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);  // Vector offset 0x20 (32)
    io_wait();
    outb(PIC1_DATA, 0x04);  // PIC2 en IRQ2
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();

    // Inicializar PIC2
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_DATA, 0x28);  // Vector offset 0x28 (40)
    io_wait();
    outb(PIC2_DATA, 0x02);  // Cascada
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Configurar máscaras: habilitar solo timer (IRQ0) y teclado (IRQ1)
    // Máscara PIC1: 0xFC = 11111100 (habilitar IRQ0 y IRQ1, deshabilitar resto)
    // Máscara PIC2: 0xFF = 11111111 (deshabilitar todos)
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

// Remapear PIC para 64-bit
void pic_remap64(void) {
    uint8_t a1, a2;
    (void)a1; (void)a2; // Variables not used in this implementation

    // Guardar máscaras originales
    a1 = inb(PIC1_DATA);
    a2 = inb(PIC2_DATA);

    // Inicializar PIC1
    outb(PIC1_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC1_DATA, 0x20);  // Vector offset 0x20 (32)
    io_wait();
    outb(PIC1_DATA, 0x04);  // PIC2 en IRQ2
    io_wait();
    outb(PIC1_DATA, ICW4_8086);
    io_wait();

    // Inicializar PIC2
    outb(PIC2_COMMAND, ICW1_INIT | ICW1_ICW4);
    io_wait();
    outb(PIC2_DATA, 0x28);  // Vector offset 0x28 (40)
    io_wait();
    outb(PIC2_DATA, 0x02);  // Cascada
    io_wait();
    outb(PIC2_DATA, ICW4_8086);
    io_wait();

    // Configurar máscaras: habilitar solo timer (IRQ0) y teclado (IRQ1)
    // Máscara PIC1: 0xFC = 11111100 (habilitar IRQ0 y IRQ1, deshabilitar resto)
    // Máscara PIC2: 0xFF = 11111111 (deshabilitar todos)
    outb(PIC1_DATA, 0xFC);
    outb(PIC2_DATA, 0xFF);
}

// Enviar EOI para 32-bit
void pic_send_eoi32(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

// Enviar EOI para 64-bit
void pic_send_eoi64(uint8_t irq) {
    if (irq >= 8) {
        outb(PIC2_COMMAND, PIC_EOI);
    }
    outb(PIC1_COMMAND, PIC_EOI);
}

// Enmascarar IRQ
void pic_mask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) | (1 << irq);
    outb(port, value);
}

// Desenmascarar IRQ
void pic_unmask_irq(uint8_t irq) {
    uint16_t port;
    uint8_t value;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }

    value = inb(port) & ~(1 << irq);
    outb(port, value);
}
