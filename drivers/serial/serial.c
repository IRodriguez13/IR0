// Simple serial driver for debugging
#include <stdint.h>

#define SERIAL_PORT_COM1 0x3F8

// I/O functions
extern uint8_t inb(uint16_t port);
extern void outb(uint16_t port, uint8_t data);

static int serial_initialized = 0;

void serial_init(void)
{
    if (serial_initialized) return;
    
    // Disable interrupts
    outb(SERIAL_PORT_COM1 + 1, 0x00);
    
    // Set baud rate divisor (38400 baud)
    outb(SERIAL_PORT_COM1 + 3, 0x80);  // Enable DLAB
    outb(SERIAL_PORT_COM1 + 0, 0x03);  // Divisor low byte
    outb(SERIAL_PORT_COM1 + 1, 0x00);  // Divisor high byte
    
    // Configure: 8 bits, no parity, one stop bit
    outb(SERIAL_PORT_COM1 + 3, 0x03);
    
    // Enable FIFO, clear them, with 14-byte threshold
    outb(SERIAL_PORT_COM1 + 2, 0xC7);
    
    // Enable IRQs, set RTS/DSR
    outb(SERIAL_PORT_COM1 + 4, 0x0B);
    
    serial_initialized = 1;
}

static int serial_is_transmit_empty(void)
{
    return inb(SERIAL_PORT_COM1 + 5) & 0x20;
}

void serial_putchar(char c)
{
    if (!serial_initialized) serial_init();
    
    while (!serial_is_transmit_empty());
    outb(SERIAL_PORT_COM1, c);
}

void serial_print(const char *str)
{
    if (!str) return;
    
    while (*str) {
        serial_putchar(*str);
        str++;
    }
}

void serial_print_hex32(uint32_t num)
{
    serial_print("0x");
    for (int i = 7; i >= 0; i--) {
        int digit = (num >> (i * 4)) & 0xF;
        char c = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        serial_putchar(c);
    }
}
void serial_print_hex64(uint64_t num)
{
    serial_print("0x");
    for (int i = 15; i >= 0; i--) {
        int digit = (num >> (i * 4)) & 0xF;
        char c = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        serial_putchar(c);
    }
}
