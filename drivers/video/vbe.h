// VBE (VESA BIOS Extensions) Framebuffer Driver Header
#pragma once

#include <stdint.h>
#include <stdbool.h>

// Initialize VBE framebuffer
int vbe_init(void);

// Clear screen with color
void vbe_clear(uint32_t color);

// Put pixel at coordinates (for graphics mode)
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color);

// Put character at coordinates
void vbe_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg);

// Print string at coordinates
void vbe_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg);

// Get framebuffer information
bool vbe_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp);

// Check if VBE is available
bool vbe_is_available(void);

// Color macros (RGB)
#define VBE_RGB(r, g, b) (((r) << 16) | ((g) << 8) | (b))
#define VBE_BLACK   0x000000
#define VBE_WHITE   0xFFFFFF
#define VBE_RED     0xFF0000
#define VBE_GREEN   0x00FF00
#define VBE_BLUE    0x0000FF
#define VBE_CYAN    0x00FFFF
#define VBE_YELLOW  0xFFFF00
#define VBE_MAGENTA 0xFF00FF
