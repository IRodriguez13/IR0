#pragma once

#include <stdint.h>

/*
 * Ring buffer sizing lives in keyboard.c (KERNEL_KBD_RING_SIZE).
 * Userspace/shared layout: KEYBOARD_BUFFER_ADDR / KEYBOARD_BUFFER_SIZE in config.h.
 */

char keyboard_buffer_get(void);
int keyboard_buffer_has_data(void);
void keyboard_buffer_clear(void);

void keyboard_handler64(void);

void keyboard_init(void);

char translate_scancode(uint8_t sc);

void set_idle_mode(int is_idle);
int is_in_idle_mode(void);
void wakeup_from_idle(void);
