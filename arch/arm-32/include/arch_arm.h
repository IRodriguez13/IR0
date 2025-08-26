#ifndef ARCH_ARM_H
#define ARCH_ARM_H

#include <stdint.h>

// Funciones específicas de ARM-32
void kmain_arm32(void);

// Constantes específicas de ARM
#define ARM_STACK_SIZE 4096
#define ARM_KERNEL_BASE 0x80000000

// Tipos específicos de ARM
typedef uint32_t arm_reg_t;
typedef uint32_t arm_addr_t;

#endif // ARCH_ARM_H
