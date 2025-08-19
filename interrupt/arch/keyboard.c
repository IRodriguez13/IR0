#include "idt.h"
#include "pic.h"
#include "io.h"
#include "../../includes/ir0/print.h"

// Buffer de teclado simple
#define KEYBOARD_BUFFER_SIZE 256
static char keyboard_buffer[KEYBOARD_BUFFER_SIZE];
static int keyboard_buffer_head = 0;
static int keyboard_buffer_tail = 0;

// Tabla de scancodes básica
static const char scancode_to_ascii[] = {
    0,    0,    '1',  '2',  '3',  '4',  '5',  '6',  // 0-7
    '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t', // 8-15 (backspace, tab)
    'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',  // 16-23
    'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',  // 24-31 (enter, ctrl)
    'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',  // 32-39
    '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',  // 40-47 (shift)
    'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',  // 48-55 (shift)
    0,    ' ',  0,    0,    0,    0,    0,    0,    // 56-63 (alt, space, caps, F1-F4)
    0,    0,    0,    0,    0,    0,    0,    0,    // 64-71 (F5-F12)
    0,    0,    0,    0,    0,    0,    0,    0,    // 72-79 (numpad)
    0,    0,    0,    0,    0,    0,    0,    0,    // 80-87
};

// Función para agregar carácter al buffer
static void keyboard_buffer_add(char c) {
    int next = (keyboard_buffer_head + 1) % KEYBOARD_BUFFER_SIZE;
    if (next != keyboard_buffer_tail) {
        keyboard_buffer[keyboard_buffer_head] = c;
        keyboard_buffer_head = next;
    }
}

// Función para obtener carácter del buffer
char keyboard_buffer_get(void) {
    if (keyboard_buffer_head == keyboard_buffer_tail) {
        return 0; // Buffer vacío
    }
    char c = keyboard_buffer[keyboard_buffer_tail];
    keyboard_buffer_tail = (keyboard_buffer_tail + 1) % KEYBOARD_BUFFER_SIZE;
    return c;
}

// Función para verificar si hay caracteres en el buffer
int keyboard_buffer_has_data(void) {
    return keyboard_buffer_head != keyboard_buffer_tail;
}

// Función para limpiar el buffer
void keyboard_buffer_clear(void) {
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
}

// Handler de interrupciones de teclado para 64-bit
void keyboard_handler64(void) {
    // Leer scancode del puerto 0x60
    uint8_t scancode = inb(0x60);
    
    // Debug removido - scancodes confirmados: backspace=0x0E, space=0x39, ctrl=0x1D, tab=0x0F
    
    // Solo procesar key press (scancode < 0x80)
    if (scancode < 0x80 && scancode < sizeof(scancode_to_ascii)) {
        char ascii = scancode_to_ascii[scancode];
        if (ascii != 0) {
            keyboard_buffer_add(ascii);
            // No mostrar aquí - el shell se encarga del echo
        }
    }
}

// Función para inicializar el teclado
void keyboard_init(void) {
    // Limpiar buffer
    keyboard_buffer_head = 0;
    keyboard_buffer_tail = 0;
    
    // Habilitar IRQ del teclado (IRQ1)
    pic_unmask_irq(IRQ_KEYBOARD);
}
