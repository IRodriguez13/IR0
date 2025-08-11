#pragma once
#include <stdint.h>


void idt_arch_set_gate(int n, uintptr_t handler, uint8_t flags);


typedef struct 
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  type_attr;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct 
{
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed)) idt_ptr_t;