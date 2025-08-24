// arch/x86-64/sources/idt_arch_x64.c - CORREGIDO
#include <arch/common/idt.h>  // Solo incluir el header común

// Array externo del IDT (definido en idt.c)
extern idt_entry_t idt[256];

// Implementación específica para 64-bit
void idt_arch_set_gate_64(int n, uintptr_t handler, uint8_t flags) 
{
    idt[n].offset_low = handler & 0xFFFF;                    // Bits 0-15
    idt[n].selector = 0x08;                                  // Código del kernel
    idt[n].ist = 0;                                          // No usar IST
    idt[n].type_attr = flags;                                // Tipo y privilegios
    idt[n].offset_mid = (handler >> 16) & 0xFFFF;            // Bits 16-31
    idt[n].offset_high = (handler >> 32) & 0xFFFFFFFF;       // Bits 32-63
    idt[n].zero = 0;                                         // Reservado
}

// Función de paginación para 64-bit (cuando la implementes)
void paging_set_cpu_64(uint64_t page_directory)
{
    // Para 64-bit: usar PML4 en lugar de page directory
    asm volatile("mov %0, %%cr3" ::"r"(page_directory));
}