// arch/x86-32/sources/idt_arch_x86.h - CORREGIDO
#pragma once

#include <stdint.h>

// Declaraciones de funciones específicas para 32-bit
void idt_arch_set_gate_32(int n, uintptr_t handler, uint8_t flags);
void paging_set_cpu(uint32_t page_directory);