/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 console backend facade implementation
 */

#include <ir0/console_backend.h>
#include <ir0/console.h>
#include <ir0/video_console.h>
#include <ir0/vt.h>
#include <ir0/vga.h>

void console_backend_init(void)
{
    console_init();
    vt_register_printk_console();
}

void console_backend_typewriter_init(void)
{
    vt_init();
}

void console_backend_clear(uint8_t color)
{
    vt_clear(color);
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
    return vt_printk_to_screen();
}

void console_backend_panic_screen_on(void)
{
    vt_printk_screen_on();
}

void console_backend_set_tty_serial_mirror(int on)
{
    tty_serial_mirror_set(on);
}

void console_backend_userspace_handoff(void)
{
    vt_printk_screen_off();
    tty_serial_mirror_handoff();
    vt_userspace_handoff();
}

void console_backend_scroll(int lines)
{
    vt_scroll(lines);
}

void console_backend_write(const char *str, size_t len, uint8_t color)
{
    size_t i;

    if (!str)
        return;

    vt_note_userspace_draw(len);

    for (i = 0; i < len; i++)
    {
        char c = str[i];

        tty_serial_mirror_bytes(&c, 1);
        if (c == '\n')
            vt_print("\n", color);
        else
            vt_putchar(c, color);
    }
}

void console_backend_write_serial(const char *str, size_t len)
{
    tty_serial_mirror_bytes(str, len);
}

void console_backend_show_cursor(uint8_t color)
{
    vt_show_cursor(color);
}

int console_backend_cursor_x(void)
{
    return vt_cursor_x();
}
