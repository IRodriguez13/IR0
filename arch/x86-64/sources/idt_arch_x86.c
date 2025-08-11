#include "idt_arch_x86.h"

extern idt_entry_t idt[256];


extern idt_entry_t idt[256];

void idt_arch_set_gate(int n, uintptr_t handler, uint8_t flags) {
    idt[n].offset_low = handler & 0xFFFF;
    idt[n].selector = 0x08;
    idt[n].zero = 0;
    idt[n].type_attr = flags;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
}

// Implementación de 32 bit pero ahora portado.

void paging_set_cpu(uint32_t page_directory)
{
    // Le tengo que decir a la CPU cómo estoy paginando y que inicie la paginación
    asm volatile("mov %0, %%cr3" ::"r"(page_directory));

    // Le digo a la CPU que active el bit de paginación
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));

    cr0 |= 0x80000000;

    asm volatile("mov %0, %%cr0" ::"r"(cr0));
}