// VBE (VESA BIOS Extensions) Framebuffer Driver
#include <stdint.h>
#include <stdbool.h>
#include <ir0/vga.h>
#include <string.h>

// VBE structures
typedef struct {
    uint16_t attributes;
    uint8_t window_a;
    uint8_t window_b;
    uint16_t granularity;
    uint16_t window_size;
    uint16_t segment_a;
    uint16_t segment_b;
    uint32_t win_func_ptr;
    uint16_t pitch;
    uint16_t width;
    uint16_t height;
    uint8_t w_char;
    uint8_t y_char;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved0;
    uint8_t red_mask;
    uint8_t red_position;
    uint8_t green_mask;
    uint8_t green_position;
    uint8_t blue_mask;
    uint8_t blue_position;
    uint8_t reserved_mask;
    uint8_t reserved_position;
    uint8_t direct_color_attributes;
    uint32_t framebuffer;
    uint32_t off_screen_mem_off;
    uint16_t off_screen_mem_size;
    uint8_t reserved1[206];
} __attribute__((packed)) vbe_mode_info_t;

// Global VBE state
static struct {
    bool initialized;
    uint32_t framebuffer_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t *fb;
} vbe_state = {0};

// Simple font (8x16 bitmap font - simplified)
static const uint8_t font_8x16[256][16] __attribute__((unused)) = {0}; // Would contain actual font data

// Initialize VBE framebuffer
int vbe_init(void)
{
    // Most GRUB bootloaders set up a framebuffer at 0xFD000000
    // with 1024x768x32 by default
    
    vbe_state.framebuffer_addr = 0xB8000; // VGA text mode for now
    vbe_state.width = 80;
    vbe_state.height = 25;
    vbe_state.pitch = 160; // 80 chars * 2 bytes
    vbe_state.bpp = 16; // Text mode
    vbe_state.fb = (uint8_t*)(uintptr_t)vbe_state.framebuffer_addr;
    vbe_state.initialized = true;
    
    return 0;
}

// Clear screen with color
void vbe_clear(uint32_t color)
{
    if (!vbe_state.initialized) return;
    
    // For VGA text mode, clear with spaces
    uint16_t *fb = (uint16_t*)vbe_state.fb;
    uint16_t entry = (uint16_t)' ' | ((uint16_t)color << 8);
    
    for (uint32_t i = 0; i < vbe_state.width * vbe_state.height; i++)
    {
        fb[i] = entry;
    }
}

// Put pixel (for graphics mode)
void vbe_putpixel(uint32_t x, uint32_t y, uint32_t color)
{
    if (!vbe_state.initialized) return;
    if (x >= vbe_state.width || y >= vbe_state.height) return;
    
    if (vbe_state.bpp == 32)
    {
        uint32_t *fb = (uint32_t*)vbe_state.fb;
        fb[y * vbe_state.width + x] = color;
    }
}

// Draw character at position
void vbe_putchar(uint32_t x, uint32_t y, char c, uint32_t fg, uint32_t bg __attribute__((unused)))
{
    if (!vbe_state.initialized) return;
    if (x >= vbe_state.width || y >= vbe_state.height) return;
    
    uint16_t *fb = (uint16_t*)vbe_state.fb;
    uint16_t entry = (uint16_t)c | ((uint16_t)fg << 8);
    fb[y * vbe_state.width + x] = entry;
}

// Print string at position
void vbe_print_at(uint32_t x, uint32_t y, const char *str, uint32_t fg, uint32_t bg)
{
    if (!vbe_state.initialized || !str) return;
    
    uint32_t col = x;
    while (*str && col < vbe_state.width)
    {
        vbe_putchar(col++, y, *str++, fg, bg);
    }
}

// Get framebuffer info
bool vbe_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp)
{
    if (!vbe_state.initialized) return false;
    if (width) *width = vbe_state.width;
    if (height) *height = vbe_state.height;
    if (bpp) *bpp = vbe_state.bpp;
    return true;
}

// Check if VBE is available
bool vbe_is_available(void)
{
    return vbe_state.initialized;
}
