// drivers/timer/pit/pit.c - VERSIÓN ESTABLE
#include "pit.h"

// Variable global para ticks del PIT
uint64_t pit_ticks = 0;
#include "../../arch/common/idt.h"
#include <print.h>
#include <panic/panic.h>
#include <arch_interface.h> // Para outb
#include "../../../includes/ir0/print.h"
#include "../../../arch/x86-64/sources/arch_x64.h"
#define PIT_FREC 1193180

extern void timer_stub();

static uint32_t ticks = 0;

// REMOVIDO: time_handler() - ahora está en isr_handlers.c

uint32_t get_pit_ticks(void)
{
    return ticks;
}

void increment_pit_ticks(void)
{
    ticks++;
}

// Inicializar PIC (Programmable Interrupt Controller) - VERSIÓN CONSERVADORA
void init_pic(void)
{
    print_colored("[PIC] Inicializando Programmable Interrupt Controller...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Guardar máscaras actuales
    uint8_t mask1 = inb(0x21);
    uint8_t mask2 = inb(0xA1);

    print("[PIC] Máscaras originales - PIC1: ");
    print_hex_compact(mask1);
    print(" PIC2: ");
    print_hex_compact(mask2);
    print("\n");

    // Deshabilitar todas las interrupciones temporalmente
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

    print("[PIC] Máscaras finales - PIC1: ");
    print_hex_compact(inb(0x21));
    print(" PIC2: ");
    print_hex_compact(inb(0xA1));
    print("\n");

    print_success("[PIC] Configurado correctamente (modo estable)\n");
}

void init_PIT(uint32_t frequency)
{
    print_colored("[PIT] Inicializando Programmable Interval Timer...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    // Inicializar PIC primero
    init_pic();

    // Calcular divisor para la frecuencia deseada
    uint32_t divisor = PIT_FREC / frequency;

    print("PIT frequency: ");
    print_hex_compact(frequency);
    print(" Hz, divisor: ");
    print_hex_compact(divisor);
    print("\n");

    // Configurar PIT
    outb(0x43, 0x36);                  // Comando: canal 0, lohi, modo 3
    outb(0x40, divisor & 0xFF);        // Byte bajo del divisor
    outb(0x40, (divisor >> 8) & 0xFF); // Byte alto del divisor

    // Habilitar interrupción del timer (IRQ 0)
    uint8_t mask = inb(0x21);
    mask &= ~(1 << 0); // Habilitar IRQ 0 (timer)
    outb(0x21, mask);

    print_success("[PIT] Configurado correctamente con interrupciones habilitadas\n");
}
