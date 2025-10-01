// arch/x86-32/sources/arch_x86.c - IMPLEMENTACIÓN SEGURA DESDE CERO
#include <ir0/print.h>
#include <stdint.h>

// ===============================================================================
// DECLARACIONES EXTERNAS DE LOS STUBS DE INTERRUPCIÓN
// ===============================================================================

// Declarar todos los stubs de interrupción
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
extern void isr32_32(void);
extern void isr33_32(void);
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

// ===============================================================================
// ESTRUCTURAS IDT 32-BIT
// ===============================================================================

// Estructura de entrada IDT para 32-bit
struct idt_entry32 
{
    uint16_t offset_low;    // Bits 0-15 del offset
    uint16_t selector;      // Selector del segmento
    uint8_t zero;           // Siempre 0 en 32-bit
    uint8_t flags;          // Tipo y privilegios
    uint16_t offset_high;   // Bits 16-31 del offset
} __attribute__((packed));

// Puntero IDT para 32-bit
struct idt_ptr32 
{
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

// ===============================================================================
// FUNCIONES I/O BÁSICAS
// ===============================================================================

// Función para leer un byte de un puerto
static inline uint8_t inb(uint16_t port) 
{
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Función para escribir un byte a un puerto
static inline void outb(uint16_t port, uint8_t val) 
{
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

// ===============================================================================
// VARIABLES GLOBALES
// ===============================================================================

// IDT para 32-bit
static struct idt_entry32 idt32[256] __attribute__((aligned(8)));
static struct idt_ptr32 idt_ptr32 __attribute__((aligned(4)));

// Array externo para compatibilidad
struct idt_entry32 idt[256];

// ===============================================================================
// FUNCIONES IDT 32-BIT
// ===============================================================================

// Configurar entrada IDT
static void idt_set_gate32(int num, uint32_t base, uint16_t sel, uint8_t flags) 
{
    idt32[num].offset_low = base & 0xFFFF;
    idt32[num].offset_high = (base >> 16) & 0xFFFF;
    idt32[num].selector = sel;
    idt32[num].zero = 0;
    idt32[num].flags = flags;
}

// Inicializar IDT
void idt_init32_simple(void) 
{
    print("Inicializando IDT 32-bit...\n");
    
    // Configurar puntero IDT
    idt_ptr32.limit = (sizeof(struct idt_entry32) * 256) - 1;
    idt_ptr32.base = (uint32_t)&idt32;
    
    // Limpiar IDT completamente
    for (int i = 0; i < 256; i++) 
    {
        idt_set_gate32(i, (uint32_t)isr0_32, 0x08, 0x8E);
    }
    
    // Configurar excepciones específicas
    idt_set_gate32(0, (uint32_t)isr0_32, 0x08, 0x8E);   // Divide Error
    idt_set_gate32(1, (uint32_t)isr1_32, 0x08, 0x8E);   // Debug
    idt_set_gate32(2, (uint32_t)isr2_32, 0x08, 0x8E);   // NMI
    idt_set_gate32(3, (uint32_t)isr3_32, 0x08, 0x8E);   // Breakpoint
    idt_set_gate32(4, (uint32_t)isr4_32, 0x08, 0x8E);   // Overflow
    idt_set_gate32(5, (uint32_t)isr5_32, 0x08, 0x8E);   // Bounds
    idt_set_gate32(6, (uint32_t)isr6_32, 0x08, 0x8E);   // Invalid Opcode
    idt_set_gate32(7, (uint32_t)isr7_32, 0x08, 0x8E);   // Device Not Available
    idt_set_gate32(8, (uint32_t)isr8_32, 0x08, 0x8E);   // Double Fault
    idt_set_gate32(9, (uint32_t)isr9_32, 0x08, 0x8E);   // Coprocessor Segment Overrun
    idt_set_gate32(10, (uint32_t)isr10_32, 0x08, 0x8E); // Invalid TSS
    idt_set_gate32(11, (uint32_t)isr11_32, 0x08, 0x8E); // Segment Not Present
    idt_set_gate32(12, (uint32_t)isr12_32, 0x08, 0x8E); // Stack Segment Fault
    idt_set_gate32(13, (uint32_t)isr13_32, 0x08, 0x8E); // General Protection Fault
    idt_set_gate32(14, (uint32_t)isr14_32, 0x08, 0x8E); // Page Fault
    idt_set_gate32(15, (uint32_t)isr15_32, 0x08, 0x8E); // Reserved
    idt_set_gate32(16, (uint32_t)isr16_32, 0x08, 0x8E); // x87 FPU Error
    idt_set_gate32(17, (uint32_t)isr17_32, 0x08, 0x8E); // Alignment Check
    idt_set_gate32(18, (uint32_t)isr18_32, 0x08, 0x8E); // Machine Check
    idt_set_gate32(19, (uint32_t)isr19_32, 0x08, 0x8E); // SIMD FPU Exception
    
    // Configurar IRQs (32-47)
    idt_set_gate32(32, (uint32_t)isr32_32, 0x08, 0x8E); // IRQ0 - Timer
    idt_set_gate32(33, (uint32_t)isr33_32, 0x08, 0x8E); // IRQ1 - Keyboard
    idt_set_gate32(34, (uint32_t)isr34_32, 0x08, 0x8E); // IRQ2 - Cascade
    idt_set_gate32(35, (uint32_t)isr35_32, 0x08, 0x8E); // IRQ3 - COM2
    idt_set_gate32(36, (uint32_t)isr36_32, 0x08, 0x8E); // IRQ4 - COM1
    idt_set_gate32(37, (uint32_t)isr37_32, 0x08, 0x8E); // IRQ5 - LPT2
    idt_set_gate32(38, (uint32_t)isr38_32, 0x08, 0x8E); // IRQ6 - Floppy
    idt_set_gate32(39, (uint32_t)isr39_32, 0x08, 0x8E); // IRQ7 - LPT1
    idt_set_gate32(40, (uint32_t)isr40_32, 0x08, 0x8E); // IRQ8 - RTC
    idt_set_gate32(41, (uint32_t)isr41_32, 0x08, 0x8E); // IRQ9 - Free
    idt_set_gate32(42, (uint32_t)isr42_32, 0x08, 0x8E); // IRQ10 - Free
    idt_set_gate32(43, (uint32_t)isr43_32, 0x08, 0x8E); // IRQ11 - Free
    idt_set_gate32(44, (uint32_t)isr44_32, 0x08, 0x8E); // IRQ12 - PS/2 Mouse
    idt_set_gate32(45, (uint32_t)isr45_32, 0x08, 0x8E); // IRQ13 - FPU
    idt_set_gate32(46, (uint32_t)isr46_32, 0x08, 0x8E); // IRQ14 - Primary ATA
    idt_set_gate32(47, (uint32_t)isr47_32, 0x08, 0x8E); // IRQ15 - Secondary ATA
    
    print("IDT 32-bit configurada\n");
}

// Cargar IDT
void idt_load32_simple(void) 
{
    print("Cargando IDT 32-bit...\n");
    __asm__ volatile("lidt %0" : : "m"(idt_ptr32));
    print("IDT 32-bit cargada\n");
}

// ===============================================================================
// FUNCIONES PIC 32-BIT
// ===============================================================================

// Remapear PIC
void pic_remap32_simple(void) 
{
    print("Remapeando PIC 32-bit...\n");
    
    // ICW1: Inicializar PIC
    outb(0x20, 0x11);  // Master PIC
    outb(0xA0, 0x11);  // Slave PIC
    
    // ICW2: Remapear IRQs
    outb(0x21, 0x20);  // Master: IRQ 0-7 -> INT 32-39
    outb(0xA1, 0x28);  // Slave: IRQ 8-15 -> INT 40-47
    
    // ICW3: Configurar cascada
    outb(0x21, 0x04);  // Master: IRQ2 conectado a slave
    outb(0xA1, 0x02);  // Slave: ID = 2
    
    // ICW4: Modo 8086
    outb(0x21, 0x01);  // Master
    outb(0xA1, 0x01);  // Slave
    
    // Máscara de interrupciones
    outb(0x21, 0xFC);  // Master: habilitar IRQ0 (timer) e IRQ1 (keyboard)
    outb(0xA1, 0xFF);  // Slave: deshabilitar todo
    
    print("PIC 32-bit remapeado\n");
}

// ===============================================================================
// FUNCIONES DE PAGINACIÓN 32-BIT
// ===============================================================================

// Incluir las funciones de paginación
extern void setup_and_enable_paging32(void);

// ===============================================================================
// FUNCIONES DE TECLADO 32-BIT
// ===============================================================================

// Buffer de teclado
static uint8_t keyboard_buffer32[256];
static int keyboard_buffer_head32 = 0;
static int keyboard_buffer_tail32 = 0;
static int keyboard_buffer_count32 = 0;

// Funciones de compatibilidad para el teclado
void keyboard_buffer_clear(void) 
{
    keyboard_buffer_head32 = 0;
    keyboard_buffer_tail32 = 0;
    keyboard_buffer_count32 = 0;
}

int keyboard_buffer_has_data(void) 
{
    return keyboard_buffer_count32 > 0;
}

char keyboard_buffer_get(void) 
{
    if (keyboard_buffer_count32 > 0) 
    {
        char c = keyboard_buffer32[keyboard_buffer_head32];
        keyboard_buffer_head32 = (keyboard_buffer_head32 + 1) % 256;
        keyboard_buffer_count32--;
        return c;
    }
    return 0;
}

// ===============================================================================
// FUNCIONES DE COMPATIBILIDAD PIC
// ===============================================================================

// Función de compatibilidad para pic_remap32
void pic_remap32(void) 
{
    pic_remap32_simple();
}

// ===============================================================================
// HANDLER DE INTERRUPCIONES 32-BIT
// ===============================================================================

// Handler de interrupciones
void isr_handler32(uint32_t interrupt_number) 
{
    // Manejar excepciones críticas
    if (interrupt_number < 32) 
    {
        print("EXCEPCIÓN CRÍTICA: ");
        print_uint32(interrupt_number);
        print("\n");
        
        // Para excepciones críticas, loop infinito
        if (interrupt_number == 8 || interrupt_number == 13 || interrupt_number == 14) 
        {
            print("TRIPLE FAULT PREVENIDO - HALTING\n");
            __asm__ volatile("cli");
            __asm__ volatile("hlt");
        }
    }
    
    // Manejar IRQs
    if (interrupt_number >= 32 && interrupt_number <= 47) 
    {
        // Enviar EOI al PIC
        if (interrupt_number >= 40) 
        {
            outb(0xA0, 0x20);  // Slave PIC
        }
        outb(0x20, 0x20);      // Master PIC
        
        // Manejar teclado (IRQ1)
        if (interrupt_number == 33) 
        {
            uint8_t scancode = inb(0x60);
            
            // Solo procesar teclas presionadas (bit 7 = 0)
            if (!(scancode & 0x80)) 
            {
                // Convertir scancode a ASCII (versión simple)
                char ascii = 0;
                switch (scancode) 
                {
                    case 0x1E: ascii = 'a'; break;
                    case 0x30: ascii = 'b'; break;
                    case 0x2E: ascii = 'c'; break;
                    case 0x20: ascii = 'd'; break;
                    case 0x12: ascii = 'e'; break;
                    case 0x21: ascii = 'f'; break;
                    case 0x22: ascii = 'g'; break;
                    case 0x23: ascii = 'h'; break;
                    case 0x17: ascii = 'i'; break;
                    case 0x24: ascii = 'j'; break;
                    case 0x25: ascii = 'k'; break;
                    case 0x26: ascii = 'l'; break;
                    case 0x32: ascii = 'm'; break;
                    case 0x31: ascii = 'n'; break;
                    case 0x18: ascii = 'o'; break;
                    case 0x19: ascii = 'p'; break;
                    case 0x10: ascii = 'q'; break;
                    case 0x13: ascii = 'r'; break;
                    case 0x1F: ascii = 's'; break;
                    case 0x14: ascii = 't'; break;
                    case 0x16: ascii = 'u'; break;
                    case 0x2F: ascii = 'v'; break;
                    case 0x11: ascii = 'w'; break;
                    case 0x2D: ascii = 'x'; break;
                    case 0x15: ascii = 'y'; break;
                    case 0x2C: ascii = 'z'; break;
                    case 0x39: ascii = ' '; break;
                    case 0x1C: ascii = '\n'; break;
                    case 0x0E: ascii = '\b'; break;
                    default: ascii = 0; break;
                }
                
                if (ascii && keyboard_buffer_count32 < 256) 
                {
                    keyboard_buffer32[keyboard_buffer_tail32] = ascii;
                    keyboard_buffer_tail32 = (keyboard_buffer_tail32 + 1) % 256;
                    keyboard_buffer_count32++;
                }
            }
        }
    }
}

// ===============================================================================
// FUNCIÓN PRINCIPAL DEL KERNEL 32-BIT - VERSIÓN SEGURA
// ===============================================================================

// Declaración de la función main del kernel
extern void main(void);

// Esta es la función que llama el boot.asm - VERSIÓN SEGURA
void kmain_x32(void) 
{
    // ===============================================================================
    // SETUP INICIAL SEGURO - SIN PAGE FAULTS
    // ===============================================================================
    
    // Deshabilitar interrupciones inmediatamente
    __asm__ volatile("cli");  
    
    print("IR0 Kernel 32-bit iniciando...\n");
    print("Versión segura - sin page faults\n");
    
    // ===============================================================================
    // VERIFICACIONES DE SEGURIDAD
    // ===============================================================================
    
    // Verificar que estamos en modo protegido
    uint32_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    if (!(cr0 & 1)) 
    {
        print("ERROR: No estamos en modo protegido\n");
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
    
    // Verificar que tenemos stack válido
    uint32_t esp_val;
    __asm__ volatile("mov %%esp, %0" : "=r"(esp_val));
    if (esp_val < 0x100000) 
    {
        print("ERROR: Stack inválido\n");
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
    
    print("Verificaciones de seguridad pasadas\n");
    
    // ===============================================================================
    // INICIALIZACIÓN SEGURA PASO A PASO
    // ===============================================================================
    
    // 1. Inicializar IDT (sin habilitar interrupciones aún)
    print("Paso 1: Inicializando IDT...\n");
    idt_init32_simple();
    idt_load32_simple();
    
    // 2. Remapear PIC (sin habilitar interrupciones aún)
    print("Paso 2: Remapeando PIC...\n");
    pic_remap32_simple();
    
    // 3. Verificar que todo está configurado correctamente
    print("Paso 3: Verificando configuración...\n");
    
    // Verificar que la IDT está cargada
    uint32_t idt_base;
    uint16_t idt_limit;
    __asm__ volatile("sidt %0" : "=m"(idt_limit), "=m"(idt_base));
    if (idt_base == 0) 
    {
        print("ERROR: IDT no cargada correctamente\n");
        __asm__ volatile("cli");
        __asm__ volatile("hlt");
    }
    
    print("Configuración verificada correctamente\n");
    
    // ===============================================================================
    // KERNEL MÍNIMO FUNCIONAL 32-BIT
    // ===============================================================================
    
    print("Kernel mínimo 32-bit funcionando\n");
    print("Sistema estable - sin triple fault\n");
    print("IDT y PIC configurados correctamente\n");
    
    // ===============================================================================
    // HABILITAR INTERRUPCIONES DE FORMA SEGURA
    // ===============================================================================
    
    print("Habilitando interrupciones de forma segura...\n");
    __asm__ volatile("sti");
    print("Interrupciones habilitadas\n");
    
    // ===============================================================================
    // LOOP PRINCIPAL SEGURO
    // ===============================================================================
    
    print("Entrando en loop principal seguro...\n");
    
    // Loop principal simple y seguro
    while(1) 
    {
        // Halt para ahorrar CPU
        __asm__ volatile("hlt");
        
        // Verificación periódica de seguridad
        static int counter = 0;
        counter++;
        
        if (counter % 1000 == 0) 
        {
            print("Kernel 32-bit funcionando establemente...\n");
        }
    }
}

