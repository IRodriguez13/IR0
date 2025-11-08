#include "idt.h"
#include "pic.h"
#include "io.h"
#include <ir0/print.h>

// Forward declarations
void wakeup_from_idle(void);

// Buffer de teclado simple
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_buffer_head = 0;
static int keyboard_buffer_tail = 0;

// Buffer compartido con Ring 3 (shell)
#define SHARED_KEYBOARD_BUFFER_ADDR 0x500000
volatile char *shared_keyboard_buffer = (volatile char *)SHARED_KEYBOARD_BUFFER_ADDR;
volatile int *shared_keyboard_buffer_pos = (volatile int *)(SHARED_KEYBOARD_BUFFER_ADDR + 256);

// Sistema de despertar del idle
static int system_in_idle_mode = 0;
static int wake_requested = 0;

// Estado de teclas modificadoras
static int shift_pressed = 0;

// Tabla de scancodes básica (solo caracteres imprimibles)
static const char scancode_to_ascii[] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',  // 0-7
    '7',  '8',  '9',  '0',  '-',  '=',  0,    0,    // 8-15 (backspace, tab - manejados por separado)
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 16-23
    'o',  'p',  '[',  ']',  0,    0,    'a',  's',  // 24-31 (enter, ctrl - manejados por separado)
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 32-39
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  // 40-47 (shift)
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  // 48-55 (shift)
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71 (F5-F12)
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79 (numpad)
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

// Tabla de scancodes con Shift presionado
static const char scancode_to_ascii_shift[] = {
    0,    0,    '!',  '@',  '#',  '$',  '%',  '^',  // 0-7
    '&',  '*',  '(',  ')',  '_',  '+',  0,    0,    // 8-15
    'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',  // 16-23
    'O',  'P',  '{',  '}',  0,    0,    'A',  'S',  // 24-31
    'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',  // 32-39
    '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',  // 40-47
    'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',  // 48-55
    0,    0,    0,    0,    0,    0,    0,    0,    // 56-63
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

// Función para traducir scancode a carácter
char translate_scancode(uint8_t sc) 
{
    switch (sc) 
    {
        case 0x0E: return '\b';  // Backspace
        case 0x0F: return '\t';  // Tab
        case 0x1C: return '\n';  // Enter
        case 0x39: return ' ';   // Space
        default:
            // Mapeo con o sin Shift
            if (sc < sizeof(scancode_to_ascii)) 
            {
                if (shift_pressed) 
                {
                    return scancode_to_ascii_shift[sc];
                }
                else 
                {
                    return scancode_to_ascii[sc];
                }
            }
            return 0; // Carácter no reconocido
    }
}

// Función para agregar carácter al buffer
static void keyboard_buffer_add(char c)
{
    int next = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != keyboard_buffer_tail) 
    {
        keyboard_buffer[keyboard_buffer_head] = c;
        keyboard_buffer_head = next;
    }
}

#ifdef __x86_64__
// Función para obtener carácter del buffer
char keyboard_buffer_get(void) 
{
    if (keyboard_buffer_head == keyboard_buffer_tail) 
    {
        return 0; // Buffer vacío
    }
    char c = keyboard_buffer[keyboard_buffer_tail];
    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Función para verificar si hay caracteres en el buffer
int keyboard_buffer_has_data(void) 
{
    return keyboard_buffer_head != keyboard_buffer_tail;
}

// Función para limpiar el buffer
void keyboard_buffer_clear(void) 
{
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
}
#endif

// Handler de interrupciones de teclado para 64-bit
void keyboard_handler64(void) 
{
    // Leer scancode del puerto 0x60
    uint8_t scancode = inb(0x60);
    
    // Detectar Shift press/release
    if (scancode == 0x2A || scancode == 0x36) 
    {
        // Left Shift (0x2A) o Right Shift (0x36) presionado
        shift_pressed = 1;
    }
    else if (scancode == 0xAA || scancode == 0xB6) 
    {
        // Left Shift (0xAA) o Right Shift (0xB6) liberado
        shift_pressed = 0;
    }
    // Solo procesar key press (scancode < 0x80)
    else if (scancode < 0x80) 
    {
        char ascii = translate_scancode(scancode);
        if (ascii != 0) 
        {
            keyboard_buffer_add(ascii);
        }
    }
    
    // Send EOI to PIC
    outb(0x20, 0x20);
}



// Función para inicializar el teclado
void keyboard_init(void) 
{
    // Limpiar buffer
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    
    // Habilitar IRQ del teclado (IRQ1)
    pic_unmask_irq(IRQ_KEYBOARD);
}


void set_idle_mode(int is_idle) 
{
    system_in_idle_mode = is_idle;
    if (is_idle) 
    {
        wake_requested = 0; // Reset wake request when entering idle
    }
}

int is_in_idle_mode(void) 
{
    return system_in_idle_mode;
}

void wakeup_from_idle(void) 
{
    if (system_in_idle_mode) {
        wake_requested = 1;
        system_in_idle_mode = 0;
      
    } else {
        print_colored("DEBUG: wakeup called but not in idle mode\n", VGA_COLOR_RED, VGA_COLOR_BLACK);
    }
}

int is_wake_requested(void) 
{
    return wake_requested;
}

void clear_wake_request(void) 
{
    wake_requested = 0;
}
