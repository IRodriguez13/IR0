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
#include <ir0/video_console.h>

static int vga_cols(void)
{
	if (console_use_framebuffer())
		return console_get_width();
	return VGA_WIDTH;
}

static int vga_rows(void)
{
	if (console_use_framebuffer())
		return console_get_height();
	return VGA_HEIGHT;
}

/*
 * Cursor compartido entre print() y typewriter/shell.
 * Permite salida coherente cuando kernel y shell escriben a la misma consola.
 */
int cursor_pos = 0;

/* Variables globales para trackear posición */
static int cursor_x = 0;
static int cursor_y = 0;
static unsigned char current_color = 0x0F; /* Blanco sobre negro */

/* Función para crear atributo de color */
static unsigned char make_color(unsigned char fg, unsigned char bg)
{
    return fg | bg << 4;
}

/*
 * putchar_at - Escribe carácter en (x,y) en la consola activa (framebuffer o
 * la pantalla de texto que registró la plataforma).
 */
static void putchar_at(char c, unsigned char color, int x, int y)
{
    console_put_cell(y, x, c, color);
}

/* Scroll screen up by one line */
void scroll()
{
    int cols = vga_cols();
    int rows = vga_rows();

    /* The framebuffer keeps the current color; text mode blanks white on black. */
    if (console_use_framebuffer())
        console_scroll_up(current_color);
    else
        console_scroll_up(make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    cursor_y = rows - 1;
    cursor_pos = cursor_y * cols + cursor_x;
}

/*
 * putchar_ex - núcleo de salida de un carácter.
 * @to_serial:    mirror a COM1 (ruta normal de putchar).
 * @force_screen: dibujar en pantalla aunque el handoff a userspace haya
 *                apagado printk_to_screen (usado por el panic para recuperar
 *                la consola FB, ver print_screen_only()).
 */
static void putchar_ex(char c, int to_serial, int force_screen)
{
    int cols = vga_cols();
    int rows = vga_rows();

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

    cursor_x = cursor_pos % cols;
    cursor_y = cursor_pos / cols;

    if (c == '\n') /* salto de línea */
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

    /* Nueva línea si llegamos al final, que sería la columna 79 o la 80-1 */
    if (cursor_x >= cols)
    {
        cursor_x = 0;
        cursor_y++;
    }

    if (cursor_y >= rows)
    {
        scroll();
    }

    cursor_pos = cursor_y * cols + cursor_x;
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
    console_clear(make_color(VGA_COLOR_WHITE, VGA_COLOR_BLACK));
    cursor_x = 0;
    cursor_y = 0;
    cursor_pos = 0;
}

