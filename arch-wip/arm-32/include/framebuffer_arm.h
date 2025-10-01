#ifndef FRAMEBUFFER_ARM_H
#define FRAMEBUFFER_ARM_H

#include "arm_types.h"

// Constantes del framebuffer VExpress-A9
#define FB_BASE_ADDR 0x60000000
#define FB_WIDTH 1024
#define FB_HEIGHT 768
#define FB_BPP 32

// Colores básicos (formato ARGB)
#define COLOR_BLACK    0xFF000000
#define COLOR_WHITE    0xFFFFFFFF
#define COLOR_RED      0xFFFF0000
#define COLOR_GREEN    0xFF00FF00
#define COLOR_BLUE     0xFF0000FF
#define COLOR_YELLOW   0xFFFFFF00
#define COLOR_CYAN     0xFF00FFFF
#define COLOR_MAGENTA  0xFFFF00FF

// Funciones del framebuffer
void fb_init(void);
void fb_clear(uint32_t color);
void fb_draw_pixel(int x, int y, uint32_t color);
void fb_draw_rect(int x, int y, int width, int height, uint32_t color);
void fb_draw_char(int x, int y, char c, uint32_t color);
void fb_draw_text(int x, int y, const char *text, uint32_t color);
void fb_show_boot_sequence(void);

#endif // FRAMEBUFFER_ARM_H
