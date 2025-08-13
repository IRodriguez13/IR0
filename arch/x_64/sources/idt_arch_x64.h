#pragma once
#include <stdint.h>

// idt_arch_x64.h
#ifndef IDT_ARCH_X64_H
#define IDT_ARCH_X64_H

typedef struct 
{
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  ist;
    uint8_t  type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct 
{
    uint16_t limit;
    uintptr_t base;
} __attribute__((packed)) idt_ptr_t;

#endif


void idt_arch_set_gate(int n, uintptr_t handler, uint8_t flags);


// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 32 bit --
void paging_set_cpu();