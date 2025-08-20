#include "print.h"

// Variables globales para trackear posición
static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char current_color = 0x0F; // Blanco sobre negro

// Función para crear atributo de color
unsigned char make_color(unsigned char fg, unsigned char bg)
{
    return fg | bg << 4;
}

// Función para poner un carácter en posición específica
void putchar_at(char c, unsigned char color, int x, int y)
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;
    int index = y * VGA_WIDTH + x;
    vga[index] = (unsigned short)c | (unsigned short)color << 8;
}

// Función para scroll (mover todo una línea arriba)
void scroll()
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;

    // Mover todas las líneas una posición arriba
    for (int i = 0; i < (VGA_HEIGHT - 1) * VGA_WIDTH; i++)
    {
        vga[i] = vga[i + VGA_WIDTH];
    }

    // Limpiar la última línea
    for (int i = (VGA_HEIGHT - 1) * VGA_WIDTH; i < VGA_HEIGHT * VGA_WIDTH; i++)
    {
        vga[i] = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK) << 8 | ' ';
    }

    cursor_y = VGA_HEIGHT - 1;
}

// Función para poner un carácter (con cursor automático)
void putchar(char c)
{
    if (c == '\n') // el famoso "salto de línea"
    {
        cursor_x = 0;
        cursor_y++;
    }
    else if (c == '\t')
    {
        cursor_x = (cursor_x + 8) & ~(8 - 1); // Salta a cualquier cloumna múltiplo de 8. TAB le decimos
    }
    else if (c == '\r') // vuelve al principio de línea sobreescribiendo lo que haya
    {
        cursor_x = 0;
    }
    else if (c == '\b') // backspace - retroceder cursor
    {
        if (cursor_x > 0)
        {
            cursor_x--;
        }
    }
    else if (c >= ' ') // Sólo acepta Caracteres imprimibles ASCII 32 en adelante (los anteriores son corruptos para la impresion).
    {
        putchar_at(c, current_color, cursor_x, cursor_y);
        cursor_x++;
    }

    // Nueva línea si llegamos al final, que sería la columna 79 o la 80-1
    if (cursor_x >= VGA_WIDTH)
    {
        cursor_x = 0;
        cursor_y++;
    }

    // Scroll si llegamos al final de la pantalla
    if (cursor_y >= VGA_HEIGHT)
    {
        scroll();
    }
}

void print(const char *str)
{
    int i = 0;
    while (str[i] != '\0')
    {
        putchar(str[i]);
        i++;
    }
}

// Funciones útiles adicionales
void print_colored(const char *str, unsigned char fg, unsigned char bg)
{
    unsigned char old_color = current_color;
    current_color = make_color(fg, bg);
    print(str);
    current_color = old_color;
}

void clear_screen()
{
    unsigned short *vga = (unsigned short *)VGA_MEMORY;
    unsigned short blank = make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK) << 8 | ' ';

    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++)
    {
        vga[i] = blank;
    }

    cursor_x = 0;
    cursor_y = 0;
}

// Función para printf básico (sin implementar aquí, pero la idea)
void set_cursor_pos(int x, int y)
{
    if (x >= 0 && x < VGA_WIDTH && y >= 0 && y < VGA_HEIGHT)
    {
        cursor_x = x;
        cursor_y = y;
    }
}

void print_error(const char *str) // Utilizo un puntero al array de chars porque es más rápido que referenciar el string sólo (qu tambien es un array de chars, pero que recibe la direcc al primer elemento en ese caso)
{
    print_colored(str, VGA_COLOR_RED, VGA_COLOR_BLACK);
}

void print_warning(const char *str) // Y lo hago constante porque no tiene por qué cambiar el mensaje en runtime viste.
{
    print_colored(str, VGA_COLOR_YELLOW, VGA_COLOR_BLACK);
}

void print_success(const char *str)
{
    print_colored(str, VGA_COLOR_GREEN, VGA_COLOR_BLACK);
}

void print_hex_compact(uint32_t num)
{
    char hex_chars[] = "0123456789ABCDEF";
    char buffer[11];
    int started = 0;
    int index = 0;

    print("0x");

    for (int i = 7; i >= 0; i--)
    {
        uint8_t nibble = (num >> (i * 4)) & 0xF;
        if (nibble != 0 || started || i == 0)
        {
            char c = hex_chars[nibble];
            buffer[index++] = c;
            started = 1;
        }
    }

    buffer[index] = '\0';
    print(buffer);
}

void print_hex64(uint64_t val)
{
    char buffer[17];
    for (int i = 0; i < 16; i++)
    {
        uint8_t nibble = (val >> ((15 - i) * 4)) & 0xF;
        buffer[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buffer[16] = '\0';
    print(buffer);
}

void print_hex32(uint32_t val)
{
    char buffer[9];
    for (int i = 0; i < 8; i++)
    {
        uint8_t nibble = (val >> ((7 - i) * 4)) & 0xF;
        buffer[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buffer[8] = '\0';
    print(buffer);
}

void print_hex8(uint8_t val)
{
    char buffer[3];
    for (int i = 0; i < 2; i++)
    {
        uint8_t nibble = (val >> ((1 - i) * 4)) & 0xF;
        buffer[i] = (nibble < 10) ? ('0' + nibble) : ('A' + nibble - 10);
    }
    buffer[2] = '\0';
    print(buffer);
}

void print_uint64(uint64_t num)
{
    char buffer[21]; // Máximo 20 dígitos para uint64_t + null terminator
    int index = 0;

    // Caso especial para 0
    if (num == 0)
    {
        print("0");
        return;
    }

    // Convertir a string (en orden inverso)
    while (num > 0)
    {
        buffer[index++] = '0' + (num % 10);
        num /= 10;
    }

    // Imprimir en orden correcto
    for (int i = index - 1; i >= 0; i--)
    {
        putchar(buffer[i]);
    }
}

void print_int32(int32_t num)
{
    if (num < 0)
    {
        putchar('-');
        num = -num;
    }

    char buffer[12]; // Máximo 11 dígitos para int32_t + null terminator
    int index = 0;

    // Caso especial para 0
    if (num == 0)
    {
        print("0");
        return;
    }

    // Convertir a string (en orden inverso)
    while (num > 0)
    {
        buffer[index++] = '0' + (num % 10);
        num /= 10;
    }

    // Imprimir en orden correcto
    for (int i = index - 1; i >= 0; i--)
    {
        putchar(buffer[i]);
    }
}

void print_uint32(uint32_t num)
{
    char buffer[11]; // Máximo 10 dígitos para uint32_t + null terminator
    int index = 0;

    // Caso especial para 0
    if (num == 0)
    {
        print("0");
        return;
    }

    // Convertir a string (en orden inverso)
    while (num > 0)
    {
        buffer[index++] = '0' + (num % 10);
        num /= 10;
    }

    // Imprimir en orden correcto
    for (int i = index - 1; i >= 0; i--)
    {
        putchar(buffer[i]);
    }
}

void delay_ms(uint32_t ms)
{
    // Delay simple usando loops
    // Aproximadamente 1ms por cada 100,000 iteraciones
    for (volatile uint32_t i = 0; i < ms * 100000; i++)
    {
        __asm__ volatile("nop");
    }
}

// Función para convertir uint a hex string
void uint_to_hex(uintptr_t value, char *hex_str)
{
    char hex_chars[] = "0123456789ABCDEF";
    int i = 0;

    // Convertir a hex
    for (int j = 7; j >= 0; j--)
    {
        uint8_t nibble = (value >> (j * 4)) & 0xF;
        hex_str[i++] = hex_chars[nibble];
    }
    hex_str[i] = '\0';
}

// Función para imprimir valores hexadecimales
void print_hex(uintptr_t value)
{
    char hex_str[20];
    uint_to_hex(value, hex_str);
    print(hex_str);
}
