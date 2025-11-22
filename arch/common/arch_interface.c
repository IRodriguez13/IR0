#include "arch_interface.h"
#include <ir0/oops.h>

// Detect MinGW-w64 cross-compilation
#if defined(__MINGW32__) || defined(__MINGW64__) || defined(_WIN32)
    #define MINGW_BUILD 1
#else
    #define MINGW_BUILD 0
#endif

// Implementaciones específicas de arquitectura
void arch_enable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("sti" ::: "memory");
    #else
        __asm__ volatile("sti");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifclr, #2
    __asm__ volatile("msr daifclr, #2" ::: "memory");
#endif
}

void arch_disable_interrupts(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD
        __asm__ __volatile__("cli" ::: "memory");
    #else
        __asm__ volatile("cli");
    #endif
#elif defined(__aarch64__)
    // ARM64: msr daifset, #2
    __asm__ volatile("msr daifset, #2" ::: "memory");
#endif
}

uint8_t inb(uint16_t port)
{
#if defined(__x86_64__) || defined(__i386__)
    uint8_t result;
    #if MINGW_BUILD
        __asm__ __volatile__("inb %1, %0" : "=a"(result) : "Nd"(port) : "memory");
    #else
        asm volatile("inb %1, %0" : "=a"(result) : "Nd"(port));
    #endif
    return result;
#elif defined(__aarch64__)
    // ARM no tiene inb
    return 0;
#endif
}

uintptr_t read_fault_address(void)
{
#if defined(__x86_64__) || defined(__i386__)
    #if MINGW_BUILD && defined(__x86_64__)
        // MinGW-w64 requires explicit 64-bit register constraint
        uint64_t addr64;
        __asm__ __volatile__("mov %%cr2, %q0" : "=r"(addr64) : : "memory");
        return (uintptr_t)addr64;
    #elif defined(__x86_64__)
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #else
        uintptr_t addr;
        asm volatile("mov %%cr2, %0" : "=r"(addr));
        return addr;
    #endif
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
    #if MINGW_BUILD
        __asm__ __volatile__("outb %0, %1" : : "a"(value), "Nd"(port) : "memory");
    #else
        asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
    #endif
#elif defined(__aarch64__)
    // ARM no tiene puertos I/O - usar MMIO
    // Implementar cuando sea necesario
#endif
}

void cpu_wait(void)
{
#if MINGW_BUILD
    __asm__ __volatile__("hlt" ::: "memory");
#else
    asm volatile("hlt");
#endif
}

// ===============================================================================
// FUNCIONES DE DIVISIÓN 64-BIT (para resolver referencias indefinidas)
// ===============================================================================

// Implementación simple de división unsigned 64-bit
uint64_t __udivdi3(uint64_t a, uint64_t b)
{
    if (b == 0)
    {
        // División por cero - en un kernel real esto debería panic
        panic("Prohibidísimo dividir por cero. Division by zero detected.");
        return 0;
    }

    uint64_t result = 0;
    uint64_t remainder = 0;

    // Algoritmo de división simple
    for (int i = 63; i >= 0; i--)
    {
        remainder = (remainder << 1) | ((a >> i) & 1);
        if (remainder >= b)
        {
            remainder -= b;
            result |= (1ULL << i);
        }
    }

    return result;
}