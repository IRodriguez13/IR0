// drivers/timer/pit/pit.c - ARREGLADO (quitar time_handler duplicado)
#include "pit.h"
#include "../../arch/common/idt.h"
#include <print.h>
#include <panic/panic.h>
#include <arch_interface.h>  // Para outb
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

void init_PIT(uint32_t frequency)
{
    print_colored("[PIT] Inicializando Programmable Interval Timer...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);
    
    // Abrir el gate para que el timer acceda a las interrupciones
    idt_set_gate(32, (uintptr_t)timer_stub, IDT_INTERRUPT_GATE_KERNEL);

    // Calcular divisor para la frecuencia deseada
    uint32_t divisor = PIT_FREC / frequency;
    
    print("PIT frequency: ");
    print_hex_compact(frequency);
    print(" Hz, divisor: ");
    print_hex_compact(divisor);
    print("\n");

    // Configurar PIT
    outb(0x43, 0x36);                    // Comando: canal 0, lohi, modo 3
    outb(0x40, divisor & 0xFF);          // Byte bajo del divisor
    outb(0x40, (divisor >> 8) & 0xFF);   // Byte alto del divisor
    
    print_success("[PIT] Configurado correctamente\n");
}
