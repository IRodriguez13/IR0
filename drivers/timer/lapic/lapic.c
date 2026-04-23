#include "lapic.h"
#include <vga.h>
#include <ir0/oops.h>
#include <stdint.h>

#define LAPIC_BASE_LEGACY   ((uintptr_t)0xFEE00000)
#define MSR_IA32_APIC_BASE  0x1BU
#define LAPIC_TIMER_REG     0x320
#define LAPIC_TIMER_DIV     0x3E0
#define LAPIC_TIMER_INIT_COUNT 0x380
#define LAPIC_TIMER_CURR_COUNT 0x390
#define LAPIC_EOI_REG       0xB0

/* Physical MMIO base; resolved from IA32_APIC_BASE when CPUID reports APIC */
static uintptr_t lapic_mmio_base;

static int lapic_cpuid_has_apic(void)
{
    uint32_t eax, ebx, ecx, edx;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1));
    return (edx & (1U << 9)) != 0;
}

static void lapic_probe_mmio_base(void)
{
    if (lapic_mmio_base != 0)
        return;

    if (!lapic_cpuid_has_apic())
    {
        lapic_mmio_base = LAPIC_BASE_LEGACY;
        return;
    }

    {
        uint32_t lo, hi;

        __asm__ volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(MSR_IA32_APIC_BASE));
        /* Bits 12:35 of the MSR hold the page-aligned physical base */
        lapic_mmio_base = (uintptr_t)(((uint64_t)(hi & 0x0FU) << 32) |
                                      ((uint64_t)lo & 0xFFFFF000ULL));
    }
}

static inline void lapic_write(uint32_t reg, uint32_t value)
{
    lapic_probe_mmio_base();
    *((volatile uint32_t *)(lapic_mmio_base + reg)) = value;
}

void lapic_init_timer(void)
{
    print_colored("[LAPIC] Inicializando temporizador local...\n", VGA_COLOR_CYAN, VGA_COLOR_BLACK);

    /* Configurar divisor del reloj del LAPIC → /16 por ejemplo */
    lapic_write(LAPIC_TIMER_DIV, 0x3); /* divisor 16 (ver tabla del datasheet Intel) */

    /* Modo de interrupción: 0x20020 = periodic mode, IRQ 32 */
    lapic_write(LAPIC_TIMER_REG, 0x20020); /* vector 32, periodic */

    /* Cargar el contador inicial */
    lapic_write(LAPIC_TIMER_INIT_COUNT, 10000000); /* este valor define frecuencia */

    print_success("[LAPIC] Temporizador local configurado.\n");
}

int lapic_available(void)
{
    return lapic_cpuid_has_apic();
}

void lapic_send_eoi(void)
{
    lapic_write(LAPIC_EOI_REG, 0);
}
