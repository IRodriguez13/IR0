/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Console abstraction (VGA + Framebuffer backends)
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Unified console: same API for VGA text (0xB8000) and framebuffer (VBE).
 */

#include "console.h"
#include "console_font.h"
#if CONFIG_ENABLE_VBE
#include <drivers/video/vbe.h>
#endif
#include <stdbool.h>
#include <string.h>

static int use_fb = 0;

/* VGA: 80x25 text buffer at 0xB8000 */
#define VGA_BUF ((volatile uint16_t *)0xB8000)

static void put_cell_vga(int row, int col, char c, uint8_t color)
{
    if (row < 0 || row >= CONSOLE_HEIGHT || col < 0 || col >= CONSOLE_WIDTH)
        return;
    uint16_t entry = (color << 8) | (uint8_t)c;
    VGA_BUF[row * CONSOLE_WIDTH + col] = entry;
}

static void scroll_up_vga(uint8_t clear_color)
{
    for (int i = 0; i < (CONSOLE_HEIGHT - 1) * CONSOLE_WIDTH; i++)
        VGA_BUF[i] = VGA_BUF[i + CONSOLE_WIDTH];
    uint16_t blank = (clear_color << 8) | ' ';
    for (int i = (CONSOLE_HEIGHT - 1) * CONSOLE_WIDTH; i < CONSOLE_HEIGHT * CONSOLE_WIDTH; i++)
        VGA_BUF[i] = blank;
}

static void clear_vga(uint8_t color)
{
    uint16_t blank = (color << 8) | ' ';
    for (int i = 0; i < CONSOLE_WIDTH * CONSOLE_HEIGHT; i++)
        VGA_BUF[i] = blank;
}

/* VGA 16-color palette (R,G,B 0-255) */
#if CONFIG_ENABLE_VBE
static const uint8_t vga_palette_rgb[16][3] = {
    {0, 0, 0}, {0, 0, 170}, {0, 170, 0}, {0, 170, 170},
    {170, 0, 0}, {170, 0, 170}, {170, 85, 0}, {170, 170, 170},
    {85, 85, 85}, {85, 85, 255}, {85, 255, 85}, {85, 255, 255},
    {255, 85, 85}, {255, 85, 255}, {255, 255, 85}, {255, 255, 255}
};

/*
 * Scale factor: 1 = 8x16 pixels/char (compact), 2 = 16x32 (larger).
 */
#define FB_SCALE 1

static void put_cell_fb(int row, int col, char c, uint8_t color)
{
    if (row < 0 || row >= CONSOLE_HEIGHT || col < 0 || col >= CONSOLE_WIDTH)
        return;

    uint32_t w, h, bpp;
    if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
        return;

    uint32_t pitch = vbe_get_pitch();
    uint8_t *fb = vbe_get_fb();
    if (!fb)
        return;

    uint32_t fg_rgb = vbe_rgb_to_pixel(
        vga_palette_rgb[color & 0x0F][0],
        vga_palette_rgb[color & 0x0F][1],
        vga_palette_rgb[color & 0x0F][2]);
    uint32_t bg_rgb = vbe_rgb_to_pixel(
        vga_palette_rgb[(color >> 4) & 0x0F][0],
        vga_palette_rgb[(color >> 4) & 0x0F][1],
        vga_palette_rgb[(color >> 4) & 0x0F][2]);

    int cw = FONT_WIDTH * FB_SCALE;
    int ch = FONT_HEIGHT * FB_SCALE;
    int px = col * cw;
    int py = row * ch;

    if (px + cw > (int)w || py + ch > (int)h)
        return;

    unsigned char idx = (unsigned char)c;
    const unsigned char *glyph = font_8x16[idx];

    for (int dy = 0; dy < FONT_HEIGHT; dy++)
    {
        uint8_t row_bits = glyph[dy];
        for (int sy = 0; sy < FB_SCALE; sy++)
        {
            uint32_t *line = (uint32_t *)(fb + (py + dy * FB_SCALE + sy) * pitch);
            for (int dx = 0; dx < FONT_WIDTH; dx++)
            {
                uint32_t pixel = (row_bits & (0x80 >> dx)) ? fg_rgb : bg_rgb;
                for (int sx = 0; sx < FB_SCALE; sx++)
                    line[px + dx * FB_SCALE + sx] = pixel;
            }
        }
    }
}

static void scroll_up_fb(uint8_t clear_color)
{
    uint32_t w, h, bpp;
    if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
        return;

    uint32_t pitch = vbe_get_pitch();
    uint8_t *fb = vbe_get_fb();
    if (!fb)
        return;

    uint8_t bg = (clear_color >> 4) & 0x0F;
    uint32_t bg_rgb = vbe_rgb_to_pixel(
        vga_palette_rgb[bg][0], vga_palette_rgb[bg][1], vga_palette_rgb[bg][2]);
    size_t line_bytes = (FONT_HEIGHT * FB_SCALE) * pitch;
    size_t move_bytes = line_bytes * (CONSOLE_HEIGHT - 1);

    memmove(fb, fb + line_bytes, move_bytes);

    uint8_t *clear_start = fb + move_bytes;
    uint32_t *p = (uint32_t *)clear_start;
    size_t clear_pixels = (line_bytes / 4);
    for (size_t i = 0; i < clear_pixels; i++)
        p[i] = bg_rgb;
}

static void clear_fb(uint8_t color)
{
    uint32_t w, h, bpp;
    if (!vbe_get_info(&w, &h, &bpp) || bpp != 32)
        return;

    uint32_t pitch = vbe_get_pitch();
    uint8_t *fb = vbe_get_fb();
    if (!fb)
        return;

    uint8_t bg = (color >> 4) & 0x0F;
    uint32_t bg_rgb = vbe_rgb_to_pixel(
        vga_palette_rgb[bg][0], vga_palette_rgb[bg][1], vga_palette_rgb[bg][2]);
    size_t fb_size = pitch * h;
    uint32_t *p = (uint32_t *)fb;
    for (size_t i = 0; i < fb_size / 4; i++)
        p[i] = bg_rgb;
}
#else
static void put_cell_fb(int row, int col, char c, uint8_t color)
{
    (void)row;
    (void)col;
    (void)c;
    (void)color;
}

static void scroll_up_fb(uint8_t clear_color)
{
    (void)clear_color;
}

static void clear_fb(uint8_t color)
{
    (void)color;
}
#endif

void console_put_cell(int row, int col, char c, uint8_t color)
{
    if (use_fb)
        put_cell_fb(row, col, c, color);
    else
        put_cell_vga(row, col, c, color);
}

void console_scroll_up(uint8_t clear_color)
{
    if (use_fb)
        scroll_up_fb(clear_color);
    else
        scroll_up_vga(clear_color);
}

void console_clear(uint8_t color)
{
    if (use_fb)
        clear_fb(color);
    else
        clear_vga(color);
}

void console_init(void)
{
#if CONFIG_ENABLE_VBE
    uint32_t w, h, bpp;
    if (vbe_is_available() && vbe_get_info(&w, &h, &bpp) && bpp == 32 && w >= 640 && h >= 400)
        use_fb = 1;
    else
        use_fb = 0;
#else
    use_fb = 0;
#endif
}

int console_use_framebuffer(void)
{
    return use_fb;
}
