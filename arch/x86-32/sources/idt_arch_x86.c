// arch/x86-32/sources/idt_arch_x86.c - CORREGIDO
#include <arch/common/idt.h>  // Solo incluir el header común

// Array externo del IDT (definido en idt.c)
extern idt_entry_t idt[256];

// Implementación específica para 32-bit
void idt_arch_set_gate_32(int n, uintptr_t handler, uint8_t flags) 
{
    idt[n].offset_low = handler & 0xFFFF;           // Bits 0-15
    idt[n].selector = 0x08;                         // Código del kernel
    idt[n].zero = 0;                                // Siempre 0 en 32-bit
    idt[n].type_attr = flags;                       // Tipo y privilegios
    idt[n].offset_high = (handler >> 16) & 0xFFFF;  // Bits 16-31
}

// Función de paginación para 32-bit
void paging_set_cpu(uint32_t page_directory)
{
    // Configurar CR3 con el directorio de páginas
    asm volatile("mov %0, %%cr3" ::"r"(page_directory));

    // Activar el bit de paginación en CR0
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000;  // Activar bit 31 (PG - Paging)
    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}