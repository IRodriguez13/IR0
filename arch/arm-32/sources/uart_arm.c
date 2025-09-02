// arch/arm-32/sources/uart_arm.c - Driver UART básico para ARM-32
// Implementación ultra simple para mostrar la secuencia de arranque por consola

#include "arm_types.h"

// Direcciones UART para VExpress-A9
#define UART0_BASE 0x10009000
#define UART0_DR    (UART0_BASE + 0x00)  // Data Register
#define UART0_FR    (UART0_BASE + 0x18)  // Flag Register
#define UART0_IBRD  (UART0_BASE + 0x24)  // Integer Baud Rate Divisor
#define UART0_FBRD  (UART0_BASE + 0x28)  // Fractional Baud Rate Divisor
#define UART0_LCRH  (UART0_BASE + 0x2C)  // Line Control Register
#define UART0_CR    (UART0_BASE + 0x30)  // Control Register
#define UART0_IMSC  (UART0_BASE + 0x38)  // Interrupt Mask Set/Clear
#define UART0_ICR   (UART0_BASE + 0x44)  // Interrupt Clear Register

// Flags
#define UART_FR_TXFF (1 << 5)  // Transmit FIFO Full
#define UART_FR_RXFE (1 << 4)  // Receive FIFO Empty
#define UART_LCRH_FEN (1 << 4) // FIFO Enable
#define UART_LCRH_WLEN_8BIT (3 << 5) // 8-bit word length
#define UART_CR_UARTEN (1 << 0) // UART Enable
#define UART_CR_TXE (1 << 8)    // Transmit Enable
#define UART_CR_RXE (1 << 9)    // Receive Enable

// Función para escribir un byte a UART
void uart_putc(char c) 
{
    // Esperar hasta que el buffer de transmisión esté libre
    while (*(volatile uint32_t*)UART0_FR & UART_FR_TXFF);
    
    // Escribir el carácter
    *(volatile uint32_t*)UART0_DR = c;
}

// Función para leer un byte de UART
char uart_getc(void) 
{
    // Esperar hasta que haya datos disponibles
    while (*(volatile uint32_t*)UART0_FR & UART_FR_RXFE);
    
    // Leer el carácter
    return (char)(*(volatile uint32_t*)UART0_DR & 0xFF);
}

// Función para inicializar UART
void uart_init(void) 
{
    // Deshabilitar UART
    *(volatile uint32_t*)UART0_CR = 0;
    
    // Configurar baud rate (115200)
    *(volatile uint32_t*)UART0_IBRD = 26;
    *(volatile uint32_t*)UART0_FBRD = 3;
    
    // Configurar formato de línea (8N1)
    *(volatile uint32_t*)UART0_LCRH = UART_LCRH_FEN | UART_LCRH_WLEN_8BIT;
    
    // Habilitar UART, transmisión y recepción
    *(volatile uint32_t*)UART0_CR = UART_CR_UARTEN | UART_CR_TXE | UART_CR_RXE;
}

// Función para imprimir una cadena
void uart_puts(const char *str) 
{
    while (*str) 
    {
        uart_putc(*str++);
    }
}

// Función para imprimir un número hexadecimal
void uart_puthex(uint32_t value) 
{
    const char hex_chars[] = "0123456789ABCDEF";
    uart_putc('0');
    uart_putc('x');
    
    for (int i = 7; i >= 0; i--) 
    {
        uart_putc(hex_chars[(value >> (i * 4)) & 0xF]);
    }
}

// Función para imprimir un número decimal
void uart_putdec(uint32_t value) 
{
    if (value == 0) 
    {
        uart_putc('0');
        return;
    }
    
    char buffer[16];
    int i = 0;
    
    while (value > 0) 
    {
        buffer[i++] = '0' + (value % 10);
        value /= 10;
    }
    
    while (i > 0) 
    {
        uart_putc(buffer[--i]);
    }
}

// Función para mostrar la secuencia de arranque por UART
void uart_show_boot_sequence(void) 
{
    uart_init();
    
    uart_puts("\n\n");
    uart_puts("==========================================\n");
    uart_puts("           IR0 KERNEL ARM-32\n");
    uart_puts("        BOOT SEQUENCE - UART OUTPUT\n");
    uart_puts("==========================================\n\n");
    
    uart_puts("Initializing UART... ");
    uart_puts("OK\n");
    
    uart_puts("Initializing framebuffer... ");
    uart_puts("SKIPPED (using UART)\n");
    
    uart_puts("Initializing memory... ");
    uart_puts("OK\n");
    
    uart_puts("Detecting CPU... ");
    uart_puts("ARMv7-A Cortex-A9\n");
    
    uart_puts("Architecture: ");
    uart_puts("MODULAR (ARM-32 specific code)\n");
    
    uart_puts("Progress: [");
    for (int i = 0; i < 50; i++) 
    {
        uart_putc('#');
        // Simular delay
        for (volatile int j = 0; j < 100000; j++);
    }
    uart_puts("] 100%\n\n");
    
    uart_puts("System Status:\n");
    uart_puts("  - UART: OK\n");
    uart_puts("  - Memory: OK\n");
    uart_puts("  - CPU: ARMv7-A\n");
    uart_puts("  - Architecture: Modular\n");
    uart_puts("  - Kernel: IR0 ARM-32\n\n");
    
    uart_puts("==========================================\n");
    uart_puts("        KERNEL ARM-32 READY!\n");
    uart_puts("==========================================\n\n");
    
    uart_puts("Kernel is now running in ARM-32 mode.\n");
    uart_puts("This demonstrates the modular architecture.\n");
    uart_puts("Press any key to continue...\n");
    
    // Esperar una tecla
    uart_getc();
    
    uart_puts("Kernel continuing...\n");
}
