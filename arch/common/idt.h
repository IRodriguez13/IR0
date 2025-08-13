// arch/common/idt.h - CORREGIR
#pragma once
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_INTERRUPT_GATE_KERNEL  0x8E
#define IDT_INTERRUPT_GATE_USER    0xEE  
#define IDT_TRAP_GATE_KERNEL       0x8F

// ✅ Definiciones portables según arquitectura
#if defined(__x86_64__)
    typedef struct {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t  ist;
        uint8_t  type_attr;
        uint16_t offset_mid;
        uint32_t offset_high;
        uint32_t zero;
    } __attribute__((packed)) idt_entry_t;

    typedef struct {
        uint16_t limit;
        uintptr_t base;
    } __attribute__((packed)) idt_ptr_t;

#elif defined(__i386__)
    typedef struct {
        uint16_t offset_low;
        uint16_t selector;
        uint8_t  zero;
        uint8_t  type_attr;
        uint16_t offset_high;
    } __attribute__((packed)) idt_entry_t;

    typedef struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) idt_ptr_t;
#else
    #error "Arquitectura no soportada"
#endif

// Funciones comunes
void idt_init();
void idt_set_gate(int n, uintptr_t handler, uint8_t flags);

// Funciones ASM específicas (declaradas extern)
extern void idt_flush(uintptr_t);
extern void isr_default(void);
extern void isr_page_fault(void);
extern void timer_stub(void);