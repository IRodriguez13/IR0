/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: console_backend.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console backend facade implementation
 */

#include <ir0/console_backend.h>
#include <drivers/video/console.h>
#include <drivers/video/typewriter.h>

void console_backend_init(void)
{
    console_init();
}

void console_backend_clear(uint8_t color)
{
    typewriter_console_clear(color);
}

int console_backend_uses_framebuffer(void)
{
    return console_use_framebuffer();
}

void console_backend_scroll(int lines)
{
    typewriter_console_scroll(lines);
}

void console_backend_write(const char *str, size_t len, uint8_t color)
{
    size_t i;

    if (!str)
        return;

    for (i = 0; i < len; i++)
    {
        if (str[i] == '\n')
            typewriter_vga_print("\n", color);
        else
            typewriter_vga_print_char(str[i], color);
    }
}

