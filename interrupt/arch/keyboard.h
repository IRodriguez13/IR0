#ifndef KEYBOARD_H
#define KEYBOARD_H

#include <stdint.h>

// Constantes del buffer de teclado
#define KEYBOARD_BUFFER_SIZE 256

// Funciones del buffer de teclado
void keyboard_buffer_add(char c);
char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);

// Handlers de interrupciones
void keyboard_handler64(void);
void keyboard_handler32(void);

// Inicialización
void keyboard_init(void);

// Función de traducción de scancodes
char translate_scancode(uint8_t sc);

#endif // KEYBOARD_H
