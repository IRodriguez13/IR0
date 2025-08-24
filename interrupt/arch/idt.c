#include "idt.h"
#include "io.h"
#include <ir0/print.h>

#ifdef __x86_64__
// Variables globales para 64-bit
struct idt_entry64 idt[256];
struct idt_ptr64 idt_ptr;

// Función para configurar una entrada IDT en 64-bit
void idt_set_gate64(uint8_t num, uint64_t base, uint16_t sel, uint8_t flags) {
    idt[num].offset_low = (uint16_t)(base & 0xFFFF);
    idt[num].offset_mid = (uint16_t)((base >> 16) & 0xFFFF);
    idt[num].offset_high = (uint32_t)((base >> 32) & 0xFFFFFFFF);
    idt[num].selector = sel;
    idt[num].ist = 0;
    idt[num].flags = flags;
    idt[num].reserved = 0;
}

// Inicializar IDT para 64-bit
void idt_init64(void) {
    idt_ptr.limit = (sizeof(struct idt_entry64) * 256) - 1;
    idt_ptr.base = (uint64_t)&idt;

    // Limpiar IDT - configurar con stubs por defecto
    for (int i = 0; i < 256; i++) {
        idt_set_gate64(i, (uint64_t)isr0_64, 0x08, 0x8E);
    }

    // Configurar stubs específicos para cada interrupción
    extern void isr0_64(void);
    extern void isr1_64(void);
    extern void isr2_64(void);
    extern void isr3_64(void);
    extern void isr4_64(void);
    extern void isr5_64(void);
    extern void isr6_64(void);
    extern void isr7_64(void);
    extern void isr8_64(void);
    extern void isr9_64(void);
    extern void isr10_64(void);
    extern void isr11_64(void);
    extern void isr12_64(void);
    extern void isr13_64(void);
    extern void isr14_64(void);
    extern void isr15_64(void);
    extern void isr16_64(void);
    extern void isr17_64(void);
    extern void isr18_64(void);
    extern void isr19_64(void);
    extern void isr20_64(void);
    extern void isr21_64(void);
    extern void isr22_64(void);
    extern void isr23_64(void);
    extern void isr24_64(void);
    extern void isr25_64(void);
    extern void isr26_64(void);
    extern void isr27_64(void);
    extern void isr28_64(void);
    extern void isr29_64(void);
    extern void isr30_64(void);
    extern void isr31_64(void);
    extern void isr32_64(void);  // Timer
    extern void isr33_64(void);  // Keyboard
    extern void isr34_64(void);
    extern void isr35_64(void);
    extern void isr36_64(void);
    extern void isr37_64(void);
    extern void isr38_64(void);
    extern void isr39_64(void);
    extern void isr40_64(void);
    extern void isr41_64(void);
    extern void isr42_64(void);
    extern void isr43_64(void);
    extern void isr44_64(void);
    extern void isr45_64(void);
    extern void isr46_64(void);
    extern void isr47_64(void);

    // Configurar excepciones (0-31)
    idt_set_gate64(0, (uint64_t)isr0_64, 0x08, 0x8E);
    idt_set_gate64(1, (uint64_t)isr1_64, 0x08, 0x8E);
    idt_set_gate64(2, (uint64_t)isr2_64, 0x08, 0x8E);
    idt_set_gate64(3, (uint64_t)isr3_64, 0x08, 0x8E);
    idt_set_gate64(4, (uint64_t)isr4_64, 0x08, 0x8E);
    idt_set_gate64(5, (uint64_t)isr5_64, 0x08, 0x8E);
    idt_set_gate64(6, (uint64_t)isr6_64, 0x08, 0x8E);
    idt_set_gate64(7, (uint64_t)isr7_64, 0x08, 0x8E);
    idt_set_gate64(8, (uint64_t)isr8_64, 0x08, 0x8E);
    idt_set_gate64(9, (uint64_t)isr9_64, 0x08, 0x8E);
    idt_set_gate64(10, (uint64_t)isr10_64, 0x08, 0x8E);
    idt_set_gate64(11, (uint64_t)isr11_64, 0x08, 0x8E);
    idt_set_gate64(12, (uint64_t)isr12_64, 0x08, 0x8E);
    idt_set_gate64(13, (uint64_t)isr13_64, 0x08, 0x8E);
    idt_set_gate64(14, (uint64_t)isr14_64, 0x08, 0x8E);
    idt_set_gate64(15, (uint64_t)isr15_64, 0x08, 0x8E);
    idt_set_gate64(16, (uint64_t)isr16_64, 0x08, 0x8E);
    idt_set_gate64(17, (uint64_t)isr17_64, 0x08, 0x8E);
    idt_set_gate64(18, (uint64_t)isr18_64, 0x08, 0x8E);
    idt_set_gate64(19, (uint64_t)isr19_64, 0x08, 0x8E);
    idt_set_gate64(20, (uint64_t)isr20_64, 0x08, 0x8E);
    idt_set_gate64(21, (uint64_t)isr21_64, 0x08, 0x8E);
    idt_set_gate64(22, (uint64_t)isr22_64, 0x08, 0x8E);
    idt_set_gate64(23, (uint64_t)isr23_64, 0x08, 0x8E);
    idt_set_gate64(24, (uint64_t)isr24_64, 0x08, 0x8E);
    idt_set_gate64(25, (uint64_t)isr25_64, 0x08, 0x8E);
    idt_set_gate64(26, (uint64_t)isr26_64, 0x08, 0x8E);
    idt_set_gate64(27, (uint64_t)isr27_64, 0x08, 0x8E);
    idt_set_gate64(28, (uint64_t)isr28_64, 0x08, 0x8E);
    idt_set_gate64(29, (uint64_t)isr29_64, 0x08, 0x8E);
    idt_set_gate64(30, (uint64_t)isr30_64, 0x08, 0x8E);
    idt_set_gate64(31, (uint64_t)isr31_64, 0x08, 0x8E);

    // Configurar IRQs (32-47)
    idt_set_gate64(32, (uint64_t)isr32_64, 0x08, 0x8E);  // Timer
    idt_set_gate64(33, (uint64_t)isr33_64, 0x08, 0x8E);  // Keyboard
    idt_set_gate64(34, (uint64_t)isr34_64, 0x08, 0x8E);
    idt_set_gate64(35, (uint64_t)isr35_64, 0x08, 0x8E);
    idt_set_gate64(36, (uint64_t)isr36_64, 0x08, 0x8E);
    idt_set_gate64(37, (uint64_t)isr37_64, 0x08, 0x8E);
    idt_set_gate64(38, (uint64_t)isr38_64, 0x08, 0x8E);
    idt_set_gate64(39, (uint64_t)isr39_64, 0x08, 0x8E);
    idt_set_gate64(40, (uint64_t)isr40_64, 0x08, 0x8E);
    idt_set_gate64(41, (uint64_t)isr41_64, 0x08, 0x8E);
    idt_set_gate64(42, (uint64_t)isr42_64, 0x08, 0x8E);
    idt_set_gate64(43, (uint64_t)isr43_64, 0x08, 0x8E);
    idt_set_gate64(44, (uint64_t)isr44_64, 0x08, 0x8E);
    idt_set_gate64(45, (uint64_t)isr45_64, 0x08, 0x8E);
    idt_set_gate64(46, (uint64_t)isr46_64, 0x08, 0x8E);
    idt_set_gate64(47, (uint64_t)isr47_64, 0x08, 0x8E);

    print("IDT inicializada para 64-bit\n");
}

// Cargar IDT para 64-bit
void idt_load64(void) {
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    print("IDT cargada para 64-bit\n");
}

#else
// Variables globales para 32-bit
struct idt_entry32 idt[256];
struct idt_ptr32 idt_ptr;

// Función para configurar una entrada IDT en 32-bit
void idt_set_gate32(uint8_t num, uint32_t base, uint16_t sel, uint8_t flags) {
    idt[num].base_lo = (base & 0xFFFF);
    idt[num].base_hi = (base >> 16) & 0xFFFF;
    idt[num].sel = sel;
    idt[num].always0 = 0;
    idt[num].flags = flags;
}

// Inicializar IDT para 32-bit
void idt_init32(void) {
    idt_ptr.limit = (sizeof(struct idt_entry32) * 256) - 1;
    idt_ptr.base = (uint32_t)&idt;

    // Limpiar IDT - configurar con stubs por defecto
    for (int i = 0; i < 256; i++) {
        idt_set_gate32(i, (uint32_t)isr0_32, 0x08, 0x8E);
    }

    // Configurar stubs específicos para cada interrupción
    extern void isr0_32(void);
    extern void isr1_32(void);
    extern void isr2_32(void);
    extern void isr3_32(void);
    extern void isr4_32(void);
    extern void isr5_32(void);
    extern void isr6_32(void);
    extern void isr7_32(void);
    extern void isr8_32(void);
    extern void isr9_32(void);
    extern void isr10_32(void);
    extern void isr11_32(void);
    extern void isr12_32(void);
    extern void isr13_32(void);
    extern void isr14_32(void);
    extern void isr15_32(void);
    extern void isr16_32(void);
    extern void isr17_32(void);
    extern void isr18_32(void);
    extern void isr19_32(void);
    extern void isr20_32(void);
    extern void isr21_32(void);
    extern void isr22_32(void);
    extern void isr23_32(void);
    extern void isr24_32(void);
    extern void isr25_32(void);
    extern void isr26_32(void);
    extern void isr27_32(void);
    extern void isr28_32(void);
    extern void isr29_32(void);
    extern void isr30_32(void);
    extern void isr31_32(void);
    extern void isr32_32(void);  // Timer
    extern void isr33_32(void);  // Keyboard
    extern void isr34_32(void);
    extern void isr35_32(void);
    extern void isr36_32(void);
    extern void isr37_32(void);
    extern void isr38_32(void);
    extern void isr39_32(void);
    extern void isr40_32(void);
    extern void isr41_32(void);
    extern void isr42_32(void);
    extern void isr43_32(void);
    extern void isr44_32(void);
    extern void isr45_32(void);
    extern void isr46_32(void);
    extern void isr47_32(void);

    // Configurar excepciones (0-31)
    idt_set_gate32(0, (uint32_t)isr0_32, 0x08, 0x8E);
    idt_set_gate32(1, (uint32_t)isr1_32, 0x08, 0x8E);
    idt_set_gate32(2, (uint32_t)isr2_32, 0x08, 0x8E);
    idt_set_gate32(3, (uint32_t)isr3_32, 0x08, 0x8E);
    idt_set_gate32(4, (uint32_t)isr4_32, 0x08, 0x8E);
    idt_set_gate32(5, (uint32_t)isr5_32, 0x08, 0x8E);
    idt_set_gate32(6, (uint32_t)isr6_32, 0x08, 0x8E);
    idt_set_gate32(7, (uint32_t)isr7_32, 0x08, 0x8E);
    idt_set_gate32(8, (uint32_t)isr8_32, 0x08, 0x8E);
    idt_set_gate32(9, (uint32_t)isr9_32, 0x08, 0x8E);
    idt_set_gate32(10, (uint32_t)isr10_32, 0x08, 0x8E);
    idt_set_gate32(11, (uint32_t)isr11_32, 0x08, 0x8E);
    idt_set_gate32(12, (uint32_t)isr12_32, 0x08, 0x8E);
    idt_set_gate32(13, (uint32_t)isr13_32, 0x08, 0x8E);
    idt_set_gate32(14, (uint32_t)isr14_32, 0x08, 0x8E);
    idt_set_gate32(15, (uint32_t)isr15_32, 0x08, 0x8E);
    idt_set_gate32(16, (uint32_t)isr16_32, 0x08, 0x8E);
    idt_set_gate32(17, (uint32_t)isr17_32, 0x08, 0x8E);
    idt_set_gate32(18, (uint32_t)isr18_32, 0x08, 0x8E);
    idt_set_gate32(19, (uint32_t)isr19_32, 0x08, 0x8E);
    idt_set_gate32(20, (uint32_t)isr20_32, 0x08, 0x8E);
    idt_set_gate32(21, (uint32_t)isr21_32, 0x08, 0x8E);
    idt_set_gate32(22, (uint32_t)isr22_32, 0x08, 0x8E);
    idt_set_gate32(23, (uint32_t)isr23_32, 0x08, 0x8E);
    idt_set_gate32(24, (uint32_t)isr24_32, 0x08, 0x8E);
    idt_set_gate32(25, (uint32_t)isr25_32, 0x08, 0x8E);
    idt_set_gate32(26, (uint32_t)isr26_32, 0x08, 0x8E);
    idt_set_gate32(27, (uint32_t)isr27_32, 0x08, 0x8E);
    idt_set_gate32(28, (uint32_t)isr28_32, 0x08, 0x8E);
    idt_set_gate32(29, (uint32_t)isr29_32, 0x08, 0x8E);
    idt_set_gate32(30, (uint32_t)isr30_32, 0x08, 0x8E);
    idt_set_gate32(31, (uint32_t)isr31_32, 0x08, 0x8E);

    // Configurar IRQs (32-47)
    idt_set_gate32(32, (uint32_t)isr32_32, 0x08, 0x8E);  // Timer
    idt_set_gate32(33, (uint32_t)isr33_32, 0x08, 0x8E);  // Keyboard
    idt_set_gate32(34, (uint32_t)isr34_32, 0x08, 0x8E);
    idt_set_gate32(35, (uint32_t)isr35_32, 0x08, 0x8E);
    idt_set_gate32(36, (uint32_t)isr36_32, 0x08, 0x8E);
    idt_set_gate32(37, (uint32_t)isr37_32, 0x08, 0x8E);
    idt_set_gate32(38, (uint32_t)isr38_32, 0x08, 0x8E);
    idt_set_gate32(39, (uint32_t)isr39_32, 0x08, 0x8E);
    idt_set_gate32(40, (uint32_t)isr40_32, 0x08, 0x8E);
    idt_set_gate32(41, (uint32_t)isr41_32, 0x08, 0x8E);
    idt_set_gate32(42, (uint32_t)isr42_32, 0x08, 0x8E);
    idt_set_gate32(43, (uint32_t)isr43_32, 0x08, 0x8E);
    idt_set_gate32(44, (uint32_t)isr44_32, 0x08, 0x8E);
    idt_set_gate32(45, (uint32_t)isr45_32, 0x08, 0x8E);
    idt_set_gate32(46, (uint32_t)isr46_32, 0x08, 0x8E);
    idt_set_gate32(47, (uint32_t)isr47_32, 0x08, 0x8E);

    print("IDT inicializada para 32-bit\n");
}

// Cargar IDT para 32-bit
void idt_load32(void) {
    __asm__ volatile("lidt %0" : : "m"(idt_ptr));
    print("IDT cargada para 32-bit\n");
}

#endif
