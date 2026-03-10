/*
 * VBE (VESA BIOS Extensions) Framebuffer Driver
 * Reference: https://wiki.osdev.org/VESA_Video_Modes
 * Multiboot framebuffer: https://wiki.osdev.org/Multiboot
 */
#include <stdint.h>
#include <stdbool.h>
#include <ir0/vga.h>
#include <ir0/multiboot.h>
#include <string.h>
#include <mm/paging.h>
#include <ir0/kmem.h>

/* Diagnostic: reason for vbe_init_from_multiboot failure (0=none, 1=mb_null, 2=no_fb_flag, 3=bad_dims, 4=map_fail) */
int vbe_fail_reason;

/* Global VBE state (OSDev: linear framebuffer from multiboot) */
static struct {
    bool initialized;
    uint32_t framebuffer_addr;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t fb_type;
    uint8_t color_info[6];  /* red_pos, red_size, green_pos, green_size, blue_pos, blue_size */
    uint8_t *fb;
} vbe_state = {0};

/*
 * vbe_init_from_multiboot - Initialize VBE from Multiboot info (OSDev)
 * @mb_info: Physical address of multiboot info structure (ebx from boot)
 *
 * When GRUB uses gfxpayload=auto or gfxpayload=1024x768, it provides
 * framebuffer info in multiboot info (flags bit 12).
 */
int vbe_init_from_multiboot(uint32_t mb_info)
{
    vbe_fail_reason = 0;
    if (!mb_info)
    {
        vbe_fail_reason = 1;
        return -1;
    }

    struct multiboot_info *mb = (struct multiboot_info *)(uintptr_t)mb_info;
    if (!(mb->flags & MULTIBOOT_FLAG_FB))
    {
        vbe_fail_reason = 2;
        return -1;
    }

    uint32_t fb_phys = (uint32_t)(mb->framebuffer_addr & 0xFFFFFFFF);
    uint32_t pitch = mb->framebuffer_pitch;
    uint32_t w = mb->framebuffer_width;
    uint32_t h = mb->framebuffer_height;
    uint8_t bpp = mb->framebuffer_bpp;

    if (w == 0 || h == 0 || bpp == 0)
    {
        vbe_fail_reason = 3;
        return -1;
    }

    /*
     * Map framebuffer if above 32MB (boot only maps 0-32MB).
     * 0xFD000000 is typical for GRUB. Map enough for full screen.
     */
    uint32_t fb_size = pitch * h;
    if (fb_size < 4096)
        fb_size = 4096;
    fb_size = (fb_size + 4095) & ~4095;

    /*
     * Framebuffer above 32MB needs mapping. Boot tables pre-map 0xFD000000-0xFE000000 (4MB).
     * If outside that range, use map_page (requires kmalloc for new page tables).
     */
    if (fb_phys >= 0x2000000)  /* Above 32MB */
    {
        uint32_t fb_end = fb_phys + fb_size;
        if (fb_phys < 0xFD000000 || fb_end > 0xFE000000)
        {
            for (uint32_t off = 0; off < fb_size; off += 4096)
            {
                if (map_page(fb_phys + off, fb_phys + off, PAGE_PRESENT | PAGE_RW) != 0)
                {
                    vbe_fail_reason = 4;  /* map_page failed */
                    return -1;
                }
            }
        }
    }

    vbe_state.framebuffer_addr = fb_phys;
    vbe_state.width = w;
    vbe_state.height = h;
    vbe_state.pitch = pitch;
    vbe_state.bpp = bpp;
    vbe_state.fb_type = mb->framebuffer_type;
    for (int i = 0; i < 6; i++)
        vbe_state.color_info[i] = mb->color_info[i];
    vbe_state.fb = (uint8_t *)(uintptr_t)fb_phys;
    vbe_state.initialized = true;

    return 0;
}

int vbe_init(void)
{
    /* Fallback: if no multiboot fb, use VGA text (80x25) */
    if (vbe_state.initialized)
        return 0;

    vbe_state.framebuffer_addr = 0xB8000;
    vbe_state.width = 80;
    vbe_state.height = 25;
    vbe_state.pitch = 160;
    vbe_state.bpp = 16;
    vbe_state.fb = (uint8_t *)(uintptr_t)0xB8000;
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

bool vbe_get_info(uint32_t *width, uint32_t *height, uint32_t *bpp)
{
    if (!vbe_state.initialized)
        return false;
    if (width)
        *width = vbe_state.width;
    if (height)
        *height = vbe_state.height;
    if (bpp)
        *bpp = vbe_state.bpp;
    return true;
}

uint32_t vbe_get_pitch(void)
{
    return vbe_state.initialized ? vbe_state.pitch : 0;
}

uint8_t *vbe_get_fb(void)
{
    return vbe_state.initialized ? vbe_state.fb : NULL;
}

uint32_t vbe_get_fb_phys(void)
{
    return vbe_state.initialized ? vbe_state.framebuffer_addr : 0;
}

uint32_t vbe_get_fb_size(void)
{
    if (!vbe_state.initialized)
        return 0;
    uint32_t size = vbe_state.pitch * vbe_state.height;
    if (size < 4096)
        size = 4096;
    return (size + 4095) & ~4095;
}

// Check if VBE is available
bool vbe_is_available(void)
{
    return vbe_state.initialized;
}

/*
 * vbe_rgb_to_pixel - Convert R,G,B to framebuffer format.
 * Uses multiboot color_info when valid; otherwise BGR (0xBBGGRR) which is
 * common on PC/QEMU/VirtualBox.
 */
uint32_t vbe_rgb_to_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (!vbe_state.initialized || vbe_state.bpp != 32)
    {
        return 0xFF000000U | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    if (vbe_state.fb_type != MULTIBOOT_FB_TYPE_RGB)
    {
        return 0xFF000000U | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    uint8_t rp = vbe_state.color_info[0];
    uint8_t rs = vbe_state.color_info[1];
    uint8_t gp = vbe_state.color_info[2];
    uint8_t gs = vbe_state.color_info[3];
    uint8_t bp = vbe_state.color_info[4];
    uint8_t bs = vbe_state.color_info[5];

    /* If color_info is all zeros, use BGR (common on PC) */
    if (rp == 0 && gp == 0 && bp == 0)
    {
        return 0xFF000000U | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    if (rs == 0)
        rs = 8;
    if (gs == 0)
        gs = 8;
    if (bs == 0)
        bs = 8;

    uint32_t rv = (uint32_t)r >> (8 - rs);
    uint32_t gv = (uint32_t)g >> (8 - gs);
    uint32_t bv = (uint32_t)b >> (8 - bs);

    return (rv << rp) | (gv << gp) | (bv << bp);
}
