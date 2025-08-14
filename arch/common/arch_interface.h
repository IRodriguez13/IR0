// arch/common/arch_interface.h - ARREGLADO
#pragma once
#include <stdint.h>

// ===============================================================================
// Interfaz común para todas las arquitecturas
// ===============================================================================

/**
 * Habilita interrupciones en la CPU ("sti" en x86)
 */
void arch_enable_interrupts(void);

/**
 * Función OUTB para escribir a puertos I/O (solo x86)
 * En ARM esto sería MMIO writes
 */
void outb(uint16_t port, uint8_t value);

/**
 * Lee la dirección que causó un page fault desde CR2 (x86)
 * Implementación específica por arquitectura
 */
uintptr_t read_fault_address(void);

/**
 * Obtiene el nombre de la arquitectura actual
 * Útil para debugging y logs
 */
const char *arch_get_name(void);

// En arch/common/arch_interface.c - 
void outb(uint16_t port, uint8_t value)
{
#if defined(__x86_64__) || defined(__i386__)
    asm volatile("outb %0, %1" ::"a"(value), "Nd"(port));
#elif defined(__aarch64__)
    // ARM no tiene puertos I/O - usar MMIO
    // Implementar cuando sea necesario
#endif
}

// ===============================================================================
// Macros de detección de arquitectura (para uso interno)
// ===============================================================================

#if defined(__x86_64__) || defined(__amd64__)
#define ARCH_X86_64
#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
#define ARCH_X86_32
#elif defined(__aarch64__)
#define ARCH_ARM64
#elif defined(__arm__)
#define ARCH_ARM32
#else
#error "Arquitectura no soportada en arch_interface.h"
#endif