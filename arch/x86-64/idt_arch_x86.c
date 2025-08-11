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

