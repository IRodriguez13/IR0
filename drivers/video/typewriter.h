/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Typewriter Effect
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Typewriter/Teletype effect for console output
 */

#ifndef TYPEWRITER_H
#define TYPEWRITER_H

#include <stdint.h>
#include <stddef.h>

/* Typewriter configuration */
#define TYPEWRITER_DELAY_FAST    2000    /* Fast typing (microseconds per char) */
#define TYPEWRITER_DELAY_NORMAL  5000    /* Normal typing */
#define TYPEWRITER_DELAY_SLOW    8000    /* Slow typing */

/* Typewriter modes */
typedef enum {
    TYPEWRITER_DISABLED = 0,
    TYPEWRITER_FAST,
    TYPEWRITER_NORMAL,
    TYPEWRITER_SLOW
} typewriter_mode_t;

/* Initialize typewriter system */
void typewriter_init(void);

/* Set typewriter mode */
void typewriter_set_mode(typewriter_mode_t mode);

/* Get current mode */
typewriter_mode_t typewriter_get_mode(void);

/* Typewriter print functions */
void typewriter_print(const char *str);
void typewriter_print_char(char c);
void typewriter_print_uint32(uint32_t num);

/* VGA typewriter functions for shell */
void typewriter_vga_print(const char *str, uint8_t color);
void typewriter_vga_print_char(char c, uint8_t color);

/* Enable/disable for specific contexts */
void typewriter_enable_for_commands(int enable);
int typewriter_is_enabled_for_commands(void);

#endif /* TYPEWRITER_H */