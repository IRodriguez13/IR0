// interrupt/idt.c - CORREGIDO (sin múltiples includes)
#include <idt.h>  // Solo este include - contiene detección automática de arquitectura
#include <arch_interface.h>

idt_entry_t idt[IDT_ENTRIES]; // Tabla de descriptores de interrupciones
idt_ptr_t idt_ptr;            // Puntero al IDT

// Funciones ASM externas (declaradas en idt.h)
extern void idt_flush(uintptr_t);
extern void isr_default();
extern void isr_page_fault();
extern void timer_stub();

void idt_init()
{
    #if defined(__x86_64__)
        idt_set_gate(14, (uintptr_t)isr_page_fault, IDT_INTERRUPT_GATE_KERNEL);
    #elif defined(__i386__)
        idt_set_gate(14, (uintptr_t)isr_page_fault, IDT_INTERRUPT_GATE_KERNEL);
    #endif

    // Inicializar la base y el límite del puntero del IDT
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (uintptr_t)&idt;  // Usar uintptr_t para compatibilidad

    // Inicializar todas las entradas con handler por defecto
    for (int i = 0; i < IDT_ENTRIES; i++)
    {
        idt_set_gate(i, (uintptr_t)isr_default, IDT_INTERRUPT_GATE_KERNEL);
    }

    // Asociar interrupciones específicas
    idt_set_gate(14, (uintptr_t)isr_page_fault, IDT_INTERRUPT_GATE_KERNEL); // Page Fault
    idt_set_gate(32, (uintptr_t)timer_stub, IDT_INTERRUPT_GATE_KERNEL);     // Timer IRQ0

    // Cargar el IDT desde ASM
    idt_flush((uintptr_t)&idt_ptr);
}