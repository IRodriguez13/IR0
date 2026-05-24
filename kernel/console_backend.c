/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console backend facade implementation
 */

#include <ir0/console_backend.h>
#include <ir0/serial_io.h>
#include <ir0/vga.h>

static int printk_to_screen = 1;

void console_backend_init(void)
{
    console_init();
}

void console_backend_typewriter_init(void)
{
    typewriter_init();
    typewriter_set_mode(TYPEWRITER_DISABLED);
}

void console_backend_clear(uint8_t color)
{
    typewriter_console_clear(color);
}

int console_backend_uses_framebuffer(void)
{
    return console_use_framebuffer();
}

int console_backend_fb_scale(void)
{
    return console_get_fb_scale();
}

int console_backend_printk_to_screen(void)
{
    return printk_to_screen;
}

void console_backend_userspace_handoff(void)
{
    printk_to_screen = 0;
    typewriter_console_clear(0x07);
    serial_print("PRINTK_TTY_SEPARATION_OK\n");
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
        {
            serial_putchar('\r');
            serial_putchar('\n');
            typewriter_vga_print("\n", color);
        }
        else
        {
            serial_putchar(str[i]);
            typewriter_vga_print_char(str[i], color);
        }
    }
}

void console_backend_show_cursor(uint8_t color)
{
    typewriter_show_cursor(color);
}

int console_backend_cursor_x(void)
{
    return typewriter_cursor_x();
}
