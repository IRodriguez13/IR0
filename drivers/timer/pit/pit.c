// drivers/timer/pit/pit.c 
#include "pit.h"

// Variable global para ticks del PIT
uint64_t pit_ticks = 0;
#include <ir0/oops.h>
#include <ir0/vga.h>
#include <arch_interface.h>
#include <arch/common/idt.h>
#include <vga.h>
#include <kernel/rr_sched.h>
#include <arch/x86-64/sources/arch_x64.h>
#define PIT_FREC 1193180

extern void timer_stub();

static uint32_t ticks = 0;

uint32_t get_pit_ticks(void)
{
    return ticks;
}

void increment_pit_ticks(void)
{
    ticks++;
}

// Inicializar PIC (Programmable Interrupt Controller)
void init_pic(void)
{

    // Guardar máscaras actuales (no utilizadas por ahora)
    (void)inb(0x21);  // Read current mask1
    (void)inb(0xA1);  // Read current mask2


    outb(0x21, 0xFF);
    outb(0xA1, 0xFF);

    // ICW1: Inicialización
    outb(0x20, 0x11); // ICW1 para PIC1
    outb(0xA0, 0x11); // ICW1 para PIC2

    // ICW2: Vector offset
    outb(0x21, 0x20); // PIC1: IRQ 0-7 -> INT 0x20-0x27
    outb(0xA1, 0x28); // PIC2: IRQ 8-15 -> INT 0x28-0x2F

    // ICW3: Cascada
    outb(0x21, 0x04); // PIC1: IRQ2 conectado a PIC2
    outb(0xA1, 0x02); // PIC2: Cascada a IRQ2 de PIC1

    // ICW4: Modo 8086
    outb(0x21, 0x01); // PIC1: Modo 8086
    outb(0xA1, 0x01); // PIC2: Modo 8086

    // MODO ESTABLE: Mantener todas las interrupciones deshabilitadas
    outb(0x21, 0xFF); // PIC1: Todas deshabilitadas
    outb(0xA1, 0xFF); // PIC2: Todas deshabilitadas


}

void init_PIT(uint32_t frequency)
{

    // Inicializar PIC primero
    init_pic();

    // Calcular divisor para la frecuencia deseada
    uint32_t divisor = PIT_FREC / frequency;


    // Configurar PIT
    outb(0x43, 0x36);                  // Comando: canal 0, lohi, modo 3
    outb(0x40, divisor & 0xFF);        // Byte bajo del divisor
    outb(0x40, (divisor >> 8) & 0xFF); // Byte alto del divisor

    // Habilitar interrupción del timer (IRQ 0)
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 0); // Habilitar IRQ 0 (timer)
    outb(0x21, mask);

}
