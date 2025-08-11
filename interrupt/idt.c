#include <idt.h>

idt_entry_t idt[IDT_ENTRIES]; // Tabla de descriptores de interrupciones (256 entradas de 8 bytes)
idt_ptr_t idt_ptr;            // y mi puntero al idt

extern void idt_flush(uintptr_t); // esta es la funcion que carga el idt en asm
extern void isr_default();
extern void isr_page_fault();



void idt_init()
{
    // Inicializo la base y el límite del puntero del idt
    idt_ptr.limit = sizeof(idt_entry_t) * IDT_ENTRIES - 1;
    idt_ptr.base = (uint32_t)&idt;

    for (int i = 0; i < IDT_ENTRIES; i++)
    {
        idt_set_gate(i, (uint32_t)isr_default, IDT_INTERRUPT_GATE_KERNEL);// Inicializa todas las entradas de la IDT con un handler genérico por defecto
    }

    //  Asocia la interrupción 14 (Page Fault) a su handler específico en asm
    idt_set_gate(14, (uint32_t)isr_page_fault, IDT_INTERRUPT_GATE_KERNEL);

    // cargo el idt desde el asm
    idt_flush((uint32_t)&idt_ptr);
}

