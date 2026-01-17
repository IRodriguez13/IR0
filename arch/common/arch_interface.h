// arch/common/arch_interface.h
#pragma once
#include <stdint.h>


void arch_enable_interrupts(void);


uint8_t inb(uint16_t port);


void outb(uint16_t port, uint8_t value);


uintptr_t read_fault_address(void);


const char *arch_get_name(void);


void cpu_wait(void);



#ifndef ARCH_X86_64
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
#endif /* ARCH_X86_64 */