/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: vbe.c
 * Description: IR0 kernel source/header file
 */

/*
 * VBE (VESA BIOS Extensions) Framebuffer Driver
 * Reference: https://wiki.osdev.org/VESA_Video_Modes
 * Multiboot framebuffer: https://wiki.osdev.org/Multiboot
 */
#include <stdint.h>
#include <stdbool.h>
#include <ir0/driver_backend.h>
#include <ir0/vga.h>
#include <ir0/boot_memory.h>
#include <string.h>
#include <ir0/paging.h>
#include <ir0/kmem.h>
#include <ir0/resource_registry.h>
#include <ir0/errno.h>

/*
 * Largest framebuffer accepted from firmware. 8K at 32 bpp is ~127 MiB; a
 * bigger descriptor is corrupt and would make us map unrelated memory.
 */
#define VBE_FB_MAX_SIZE (512ULL * 1024ULL * 1024ULL)

/*
 * Pre-mapped GRUB framebuffer window in the x86-64 boot page tables: two
 * 2 MiB pages at 0xFD000000 (pd_fb in arch/x86-64/asm/boot/boot_x64.asm).
 */
#define VBE_PREMAPPED_START 0xFD000000ULL
#define VBE_PREMAPPED_END   0xFD400000ULL
#define VBE_LOW_IDENTITY_END 0x2000000ULL

/*
 * Diagnostic: reason for vbe_init_from_boot failure (0=none, 1=no descriptor,
 * 2=no framebuffer, 3=bad geometry or range, 4=map_fail).
 */
int vbe_fail_reason;

/* Global VBE state: linear framebuffer described by the boot snapshot. */
static struct {
    bool initialized;
    uint64_t framebuffer_addr;
    uint32_t fb_size;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t bpp;
    uint8_t fb_type;
    uint8_t color_info[6];  /* red_pos, red_size, green_pos, green_size, blue_pos, blue_size */
    uint8_t *fb;
} vbe_state = {0};

/*
 * vbe_init_from_boot - Initialize VBE from the boot snapshot framebuffer
 * @bfb: Firmware framebuffer in neutral form (filled by the ISA boot port
 *       from Multiboot on x86-64 or the DTB on ARM64).
 */
int vbe_init_from_boot(const struct ir0_boot_framebuffer *bfb)
{
    vbe_fail_reason = 0;
    if (!bfb)
    {
        vbe_fail_reason = 1;
        return -1;
    }

    if (bfb->width == 0)
    {
        vbe_fail_reason = 2;
        return -1;
    }

    uint64_t fb_phys = bfb->base;
    uint32_t pitch = bfb->pitch;
    uint32_t w = bfb->width;
    uint32_t h = bfb->height;
    uint8_t bpp = bfb->bpp;
    uint64_t fb_size;
    uint64_t fb_end;

    if (w == 0 || h == 0 || bpp == 0)
    {
        vbe_fail_reason = 3;
        return -1;
    }

    /*
     * Size the mapping in 64 bits: pitch * h and base + size used to wrap
     * in 32 bits, which skipped the mapping or mapped the wrong frames.
     */
    fb_size = (uint64_t)pitch * h;
    if ((uint64_t)pitch * 8U < (uint64_t)w * bpp || fb_size > VBE_FB_MAX_SIZE)
    {
        vbe_fail_reason = 3;
        return -1;
    }
    if (fb_size < 4096)
        fb_size = 4096;
    fb_size = (fb_size + 4095U) & ~4095ULL;
    fb_end = fb_phys + fb_size;
    if (fb_end < fb_phys || (uint64_t)(uintptr_t)(fb_end - 1U) != fb_end - 1U)
    {
        vbe_fail_reason = 3;
        return -1;
    }

    /*
     * Boot tables identity-map the low 32 MiB and the GRUB window; anything
     * else needs map_page (which may allocate page tables).
     */
    if (fb_phys >= VBE_LOW_IDENTITY_END &&
        (fb_phys < VBE_PREMAPPED_START || fb_end > VBE_PREMAPPED_END))
    {
        for (uint64_t off = 0; off < fb_size; off += 4096)
        {
            if (map_page(fb_phys + off, fb_phys + off, PAGE_PRESENT | PAGE_RW) != 0)
            {
                vbe_fail_reason = 4;  /* map_page failed */
                return -1;
            }
        }
    }

    vbe_state.framebuffer_addr = fb_phys;
    vbe_state.fb_size = (uint32_t)fb_size;
    vbe_state.width = w;
    vbe_state.height = h;
    vbe_state.pitch = pitch;
    vbe_state.bpp = bpp;
    vbe_state.fb_type = bfb->type;
    vbe_state.color_info[0] = bfb->red_pos;
    vbe_state.color_info[1] = bfb->red_size;
    vbe_state.color_info[2] = bfb->green_pos;
    vbe_state.color_info[3] = bfb->green_size;
    vbe_state.color_info[4] = bfb->blue_pos;
    vbe_state.color_info[5] = bfb->blue_size;
    vbe_state.fb = (uint8_t *)(uintptr_t)fb_phys;
    vbe_state.initialized = true;

    resource_register_mmio(fb_phys, fb_end - 1U, "vbe_fb");

    return 0;
}

int vbe_init(void)
{
    /* Fallback: if the firmware gave no framebuffer, use VGA text (80x25) */
    if (vbe_state.initialized)
        return 0;

#if !IR0_LEGACY_PC_IO
    /*
     * PC VGA text MMIO is not present without legacy PC I/O. Installing this
     * fallback marks vbe "initialized" and can lure console paths into
     * touching 0xB8000 (sync abort storm from EL1 on ARM).
     */
    return -1;
#else
    vbe_state.framebuffer_addr = 0xB8000;
    vbe_state.width = 80;
    vbe_state.height = 25;
    vbe_state.pitch = 160;
    vbe_state.fb_size = 4096;
    vbe_state.bpp = 16;
    vbe_state.fb = (uint8_t *)(uintptr_t)0xB8000;
    vbe_state.initialized = true;
    return 0;
#endif
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
        uint8_t *row = (uint8_t *)vbe_state.fb + y * vbe_state.pitch;
        ((uint32_t *)row)[x] = color;
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

uint64_t vbe_get_fb_phys(void)
{
    return vbe_state.initialized ? vbe_state.framebuffer_addr : 0;
}

uint32_t vbe_get_fb_size(void)
{
    if (!vbe_state.initialized)
        return 0;
    return vbe_state.fb_size;
}

/*
 * vbe_map_supervisor - Identity-map the framebuffer into a process root
 * @root: translation root that receives supervisor RW mappings.
 *
 * Kernel console writes go through the PA==VA framebuffer pointer, so every
 * address space the kernel runs in needs all of the framebuffer pages. The
 * size was already bounded by VBE_FB_MAX_SIZE in vbe_init_from_boot().
 */
int vbe_map_supervisor(address_space_root_t root)
{
    uint64_t base = vbe_state.framebuffer_addr;
    uint64_t size = vbe_state.fb_size;

    if (!root || !vbe_state.initialized || base == 0 || size == 0)
        return 0;
    for (uint64_t off = 0; off < size; off += 4096U)
    {
        if (map_page_in_directory(root, base + off, base + off,
                                  PAGE_PRESENT | PAGE_RW) != 0)
            return -ENOMEM;
    }
    return 0;
}

// Check if VBE is available
bool vbe_is_available(void)
{
    return vbe_state.initialized;
}

/*
 * vbe_rgb_to_pixel - Convert R,G,B to framebuffer format.
 * Uses the firmware RGB field layout when valid; otherwise BGR (0xBBGGRR) which is
 * common on PC/QEMU/VirtualBox.
 */
uint32_t vbe_rgb_to_pixel(uint8_t r, uint8_t g, uint8_t b)
{
    if (!vbe_state.initialized || vbe_state.bpp != 32)
    {
        return 0xFF000000U | ((uint32_t)b << 16) | ((uint32_t)g << 8) | (uint32_t)r;
    }

    if (vbe_state.fb_type != IR0_BOOT_FB_TYPE_RGB)
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
