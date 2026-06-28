/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console backend facade implementation
 */

#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/serial_io.h>
#include <ir0/vga.h>
#include <drivers/video/console_renderer.h>

#define CONSOLE_BACKEND_READY_MSG "IR0 console ready\n"

static int printk_to_screen = 1;
static int userspace_gui_first_draw_tag;

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
    typewriter_set_mode(TYPEWRITER_FAST);
    typewriter_console_clear(CONSOLE_RENDERER_COLOR_DEFAULT);

    if (console_backend_uses_framebuffer())
        serial_print("CONSOLE_BACKEND_FB_OK\n");
    else
        serial_print("CONSOLE_BACKEND_VGA_OK\n");

    serial_print("PRINTK_SERIAL_CONSOLE_FB_HANDOFF_OK\n");
    console_backend_write(CONSOLE_BACKEND_READY_MSG,
                          sizeof(CONSOLE_BACKEND_READY_MSG) - 1,
                          CONSOLE_RENDERER_COLOR_DEFAULT);
    serial_print("CONSOLE_GUI_VISIBLE_OK\n");
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

    if (!printk_to_screen && console_backend_uses_framebuffer() &&
        len > 0 && !userspace_gui_first_draw_tag)
    {
        userspace_gui_first_draw_tag = 1;
        serial_print("BUSYBOX_GUI_TEXT_FIRST_DRAW_OK\n");
        serial_print("ASH_VISIBLE_QEMU_OK\n");
    }

    for (i = 0; i < len; i++)
    {
        char c = str[i];

        if (c == '\n')
        {
            serial_putchar('\r');
            serial_putchar('\n');
            typewriter_vga_print("\n", color);
        }
        else
        {
            serial_putchar(c);
            typewriter_vga_print_char(c, color);
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
