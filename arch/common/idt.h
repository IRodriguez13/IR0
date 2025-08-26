// arch/common/idt.h - REFACTORIZADO (detección automática)
#pragma once
#include <stdint.h>

#define IDT_ENTRIES 256
#define IDT_INTERRUPT_GATE_KERNEL 0x8E
#define IDT_INTERRUPT_GATE_USER 0xEE
#define IDT_TRAP_GATE_KERNEL 0x8F

// ===============================================================================
// DETECCIÓN AUTOMÁTICA DE ARQUITECTURA Y DEFINICIONES ESPECÍFICAS
// ===============================================================================

#if defined(__x86_64__) || defined(__amd64__)
// ===== ARQUITECTURA 64-BIT =====
typedef struct
{
    uint16_t offset_low;  // Bits 0-15 del offset
    uint16_t selector;    // Selector de segmento
    uint8_t ist;          // Interrupt Stack Table (solo 64-bit)
    uint8_t type_attr;    // Tipo y atributos
    uint16_t offset_mid;  // Bits 16-31 del offset
    uint32_t offset_high; // Bits 32-63 del offset (solo 64-bit)
    uint32_t zero;        // Reservado (debe ser 0)
} __attribute__((packed)) idt_entry_t;

typedef struct
{
    uint16_t limit;
    uintptr_t base; // 64-bit address
} __attribute__((packed)) idt_ptr_t;

// Función específica para 64-bit
void idt_arch_set_gate_64(int n, uintptr_t handler, uint8_t flags);
#define idt_set_gate(n, handler, flags) idt_arch_set_gate_64(n, handler, flags)

#elif defined(__i386__) || defined(__i486__) || defined(__i586__) || defined(__i686__)
// ===== ARQUITECTURA 32-BIT =====
typedef struct
{
    uint16_t offset_low;  // Bits 0-15 del offset
    uint16_t selector;    // Selector de segmento
    uint8_t zero;         // Siempre 0 en x86-32
    uint8_t type_attr;    // Tipo y atributos
    uint16_t offset_high; // Bits 16-31 del offset
} __attribute__((packed)) idt_entry_t;

typedef struct
{
    uint16_t limit;
    uint32_t base; // 32-bit address
} __attribute__((packed)) idt_ptr_t;

// Función específica para 32-bit
void idt_arch_set_gate_32(int n, uintptr_t handler, uint8_t flags);
#define idt_set_gate(n, handler, flags) idt_arch_set_gate_32(n, handler, flags)

#else
#error "Arquitectura no soportada para IDT"
#endif

// ===============================================================================
// FUNCIONES COMUNES (independientes de arquitectura)
// ===============================================================================

void idt_init();

// Funciones ASM específicas (declaradas extern)
extern void idt_flush(uintptr_t);
extern void isr_default(void);
extern void isr_page_fault(void);
extern void timer_stub(void);