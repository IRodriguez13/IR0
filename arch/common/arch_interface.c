#include "arch_interface.h"

// Implementaciones específicas de arquitectura
void arch_enable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("sti");
#elif defined(__aarch64__)
    // ARM64: msr daifclr, #2
    __asm__ volatile("msr daifclr, #2" ::: "memory");
#endif
}

void arch_disable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    __asm__ volatile("cli");

#elif defined(__aarch64__)

    // ARM64: msr daifset, #2
    __asm__ volatile("msr daifset, #2" ::: "memory");

#endif
}

uint8_t inb(uint16_t port)
{
#if defined(__x86_64__) || defined(__i386__)
    uint8_t result;
    asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    return result;
#elif defined(__aarch64__)
    // ARM no tiene inb
    return 0;
#endif
}

uintptr_t read_fault_address(void)
{
#if defined(__x86_64__) || defined(__i386__)

    uintptr_t addr;
    asm volatile("mov %%cr2, %0" : "=r"(addr));
    return addr;

#elif defined(__aarch64__)
    // ARM64: leer FAR_EL1 register
    uint64_t addr;
    asm volatile("mrs %0, far_el1" : "=r"(addr));
    return addr;
#else
    return 0;
#endif
}

const char *arch_get_name(void)
{
#if defined(__x86_64__)
    return "x86-64 (amd64)";
#elif defined(__i386__)
    return "x86-32 (i386)";
#elif defined(__aarch64__)
    return "ARM64 (aarch64)";
#elif defined(__arm__)
    return "ARM32";
#else
    return "Unknown Architecture";
#endif
}

void outb(uint16_t port, uint8_t value)
{
#if defined(__x86_64__) || defined(__i386__)
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
#elif defined(__aarch64__)
    // ARM no tiene puertos I/O - usar MMIO
    // Implementar cuando sea necesario
#endif
}

// uint8_t inb(uint16_t port)
// {
//     uint8_t value;
//     asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
//     return value;
// }

void cpu_wait(void)
{
    asm volatile("hlt");
}

// ===============================================================================
// FUNCIONES DE DIVISIÓN 64-BIT (para resolver referencias indefinidas)
// ===============================================================================

// Implementación simple de división unsigned 64-bit
uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    if (b == 0) {
        // División por cero - en un kernel real esto debería panic
        return 0;
    }
    
    uint64_t result = 0;
    uint64_t remainder = 0;
    
    // Algoritmo de división simple
    for (int i = 63; i >= 0; i--) {
        remainder = (remainder << 1) | ((a >> i) & 1);
        if (remainder >= b) {
            remainder -= b;
            result |= (1ULL << i);
        }
    }
    
    return result;
}