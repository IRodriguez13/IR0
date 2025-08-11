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


// 32 bit

// Funciones ASM específicas de arquitectura
extern void idt_flush(uintptr_t);
extern void isr_default(void);
extern void isr_page_fault(void);
extern void timer_stub(void);

// Esta función le dice a la CPU que empiece a paginar configurando el directorio. -- 64 bit --
void paging_set_cpu();

