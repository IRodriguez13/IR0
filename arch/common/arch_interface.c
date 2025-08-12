// arch/common/arch_interface.c
#include "arch_interface.h"

#if defined(__x86_64__) || defined(__i386__)
    // Incluir las implementaciones específicas de x86
    #include "../arch/x86-32/sources/arch_x86.h"    // Para 32-bit
    #include "../arch/x_64/sources/arch_x64.h"    // Para 64-bit
#elif defined(__aarch64__) || defined(__arm__)
    #include "../arm64/sources/arch_arm64.h"   // Para futuro ARM
#endif

// Implementación común que llama a las específicas de cada arquitectura
void arch_enable_interrupts(void) {
    #if defined(__x86_64__) || defined(__i386__)
        __asm__ volatile("sti");
    #elif defined(__aarch64__)
        // ARM64: msr daifclr, #2
    #endif
}

static inline void outb(uint16_t port, uint8_t value) {
    #if defined(__x86_64__) || defined(__i386__)
        asm volatile("outb %0, %1"::"a"(value),"Nd"(port));
    #elif defined(__aarch64__)
        // ARM no tiene outb, sería MMIO writes
    #endif
}