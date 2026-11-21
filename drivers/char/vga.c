/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vga.c
 * Description: IR0 kernel source/header file
 */

#include <ir0/vga.h>
#include <ir0/serial_io.h>
#include <ir0/console_backend.h>
#include <ir0/typewriter.h>

static unsigned char current_color = 0x0F; /* Blanco sobre negro */

/* Función para crear atributo de color */
static unsigned char make_color(unsigned char fg, unsigned char bg)
{
    return fg | bg << 4;
}

/*
 * putchar_ex - núcleo de salida de un carácter.
 * @to_serial:    mirror a COM1 (ruta normal de putchar).
 * @force_screen: dibujar en pantalla aunque el handoff a userspace haya
 *                apagado printk_to_screen (usado por el panic para recuperar
 *                la consola FB, ver print_screen_only()).
 *
 * La pantalla la dibuja la VT (cursor, scroll y scrollback propios).
 */
static void putchar_ex(char c, int to_serial, int force_screen)
{
    if (to_serial)
    {
        if (c == '\n')
        {
            serial_putchar('\r');
            serial_putchar('\n');
        }
        else
            serial_putchar(c);
    }

    if (!force_screen && !console_backend_printk_to_screen())
        return;

    typewriter_putchar_attr(c, current_color);
}

/* Función para poner un carácter (con cursor automático) */
static void putchar(char c)
{
    putchar_ex(c, 1, 0);
}

/*
 * print_screen_only - escribe en la consola (FB/VGA) sin tocar serial y sin
 * respetar el gate printk_to_screen. Sólo para el panic: tras el handoff a
 * userspace printk_to_screen queda apagado, así que print()/klog no dibujan
 * en la pantalla GTK; esta ruta recupera la salida a pantalla del volcado.
 */
void print_screen_only(const char *str)
{
    int i;

    if (!str)
        return;
    for (i = 0; str[i] != '\0'; i++)
        putchar_ex(str[i], 0, 1);
}

void console_puts(const char *str)
{
    int i = 0;
    while (str[i] != '\0')
    {
        putchar(str[i]);
        i++;
    }
}

/* Funciones útiles adicionales */
void print_colored(const char *str, unsigned char fg, unsigned char bg)
{
    unsigned char old_color = current_color;
    current_color = make_color(fg, bg);
    print(str);
    current_color = old_color;
}

void clear_screen()
{
    typewriter_console_clear(make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
}
