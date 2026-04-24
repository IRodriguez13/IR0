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

