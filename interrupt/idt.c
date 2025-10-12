// interrupt/idt.c - 
#include <arch/common/idt.h>

// IDT básica
idt_entry_t idt[256];
idt_ptr_t idt_ptr;

extern void isr_stub_0();
extern void isr_stub_1();
extern void isr_stub_2();
extern void isr_stub_3();
extern void isr_stub_4();
extern void isr_stub_5();
extern void isr_stub_6();
extern void isr_stub_7();
extern void isr_stub_8();
extern void isr_stub_9();
extern void isr_stub_10();
extern void isr_stub_11();
extern void isr_stub_12();
extern void isr_stub_13();
extern void isr_stub_14();
extern void isr_stub_15();
extern void isr_stub_16();
extern void isr_stub_17();
extern void isr_stub_18();
extern void isr_stub_19();
extern void isr_stub_20();
extern void isr_stub_21();
extern void isr_stub_22();
extern void isr_stub_23();
extern void isr_stub_24();
extern void isr_stub_25();
extern void isr_stub_26();
extern void isr_stub_27();
extern void isr_stub_28();
extern void isr_stub_29();
extern void isr_stub_30();
extern void isr_stub_31();
extern void isr_stub_32(); // Timer
extern void isr_stub_33(); // Keyboard

// Función ASM para cargar IDT
extern void idt_load(uintptr_t idt_ptr);

// Handler C simple para todas las interrupciones
void isr_handler(uint8_t int_no)
{
    // Manejo específico para excepción 13 (General Protection Fault)
    if (int_no == 13)
    {
        // Evitar bucle infinito - solo mostrar error y continuar
        // Usar una función simple que no cause más excepciones
        volatile uint32_t *vga = (uint32_t *)0xB8000;
        if (vga)
        {
            vga[0] = 0x0F470F47; // "GG" en blanco
            vga[1] = 0x0F500F50; // "PP" en blanco
            vga[2] = 0x0F460F46; // "FF" en blanco
            vga[3] = 0x0F200F20; // "  " en blanco
        }

        // Enviar EOI y retornar inmediatamente
        if (int_no >= 40)
        {
            __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0xA0));
        }
        __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0x20));
        return;
    }

    // Manejo específico para excepción 14 (Page Fault)
    if (int_no == 14)
    {
        // Evitar bucle infinito - solo mostrar error y continuar
        // Usar una función simple que no cause más excepciones
        volatile uint32_t *vga = (uint32_t *)0xB8000;
        if (vga)
        {
            vga[0] = 0x0F500F50; // "PP" en blanco
            vga[1] = 0x0F460F46; // "FF" en blanco
            vga[2] = 0x0F200F20; // "  " en blanco
            vga[3] = 0x0F200F20; // "  " en blanco
        }

        // Enviar EOI y retornar inmediatamente
        if (int_no >= 40)
        {
            __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0xA0));
        }
        __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0x20));
        return;
    }

    // Para otras interrupciones, manejo normal
    volatile uint32_t *vga = (uint32_t *)0xB8000;
    if (vga)
    {
        vga[0] = 0x0F490F49; // "II" en blanco
        vga[1] = 0x0F520F52; // "RR" en blanco
        vga[2] = 0x0F200F20; // "  " en blanco
    }

    // Enviar EOI si es IRQ
    if (int_no >= 32)
    {
        if (int_no >= 40)
        {
            // Slave PIC
            __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0xA0));
        }
        // Master PIC
        __asm__ volatile("outb %%al, %%dx" : : "a"(0x20), "d"(0x20));
    }
}

// Configurar una entrada del IDT (función local, no macro)
static void idt_set_gate_local(uint8_t num, uintptr_t handler, uint8_t flags)
{
#if defined(__x86_64__)
    // Estructura IDT para 64-bit
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_mid = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((handler >> 32) & 0xFFFFFFFF);
    idt[num].selector = 0x08; // Kernel code segment
    idt[num].ist = 0;         // No IST
    idt[num].type_attr = flags;
    idt[num].zero = 0;
#else
    // Estructura IDT para 32-bit
    idt[num].offset_low = (uint16_t)(handler & 0xFFFF);
    idt[num].offset_high = (uint16_t)((handler >> 16) & 0xFFFF);
    idt[num].selector = 0x08; // Kernel code segment
    idt[num].zero = 0;        // Siempre 0 en 32-bit
    idt[num].type_attr = flags;
#endif
}

// Inicializar IDT mínima
void idt_init()
{
    // Configurar puntero IDT
    idt_ptr.limit = sizeof(idt_entry_t) * 256 - 1;
    idt_ptr.base = (uintptr_t)&idt;

    // Limpiar toda la IDT
    for (int i = 0; i < 256; i++)
    {
        idt_set_gate_local(i, (uintptr_t)isr_stub_0, 0x8E); // Interrupt gate
    }

    // Configurar stubs específicos para excepciones críticas
    idt_set_gate_local(0, (uintptr_t)isr_stub_0, 0x8E);   // Divide by zero
    idt_set_gate_local(6, (uintptr_t)isr_stub_6, 0x8E);   // Invalid opcode
    idt_set_gate_local(8, (uintptr_t)isr_stub_8, 0x8E);   // Double fault
    idt_set_gate_local(13, (uintptr_t)isr_stub_13, 0x8E); // General protection
    idt_set_gate_local(14, (uintptr_t)isr_stub_14, 0x8E); // Page fault

    // Configurar IRQs (mapped to 32-47)
    idt_set_gate_local(32, (uintptr_t)isr_stub_32, 0x8E); // Timer
    idt_set_gate_local(33, (uintptr_t)isr_stub_33, 0x8E); // Keyboard

    // Cargar IDT
    idt_load((uintptr_t)&idt_ptr);
}