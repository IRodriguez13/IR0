/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: devfs_fb.c
 * Description: framebuffer device node
 */

#include "devfs_internal.h"

/*
 * /dev/fb0 - Framebuffer (OSDev / Linux fbdev)
 * write: copy pixels to framebuffer
 * ioctl(FBIOGET_VSCREENINFO): get width, height, bpp, pitch
 */
static int64_t dev_fb0_read(devfs_entry_t *entry, void *buf, size_t count, off_t offset)
{
    (void)entry; (void)buf; (void)count; (void)offset;
    return 0;
}

static int64_t dev_fb0_open(devfs_entry_t *entry, int flags)
{
    (void)entry;
    (void)flags;
    if (!ir0_fb_is_available())
        return -ENODEV;
    return 0;
}

static int64_t dev_fb0_write_simple(devfs_entry_t *entry, const void *buf, size_t count, off_t offset)
{
    (void)entry;
    return ir0_fb_write_bytes((size_t)offset, buf, count);
}

static int64_t dev_fb0_ioctl(devfs_entry_t *entry, uint64_t request, void *arg)
{
    struct ir0_fb_info info;

    (void)entry;
    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (request == FBIOGET_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var_info;

        memset(&var_info, 0, sizeof(var_info));
        var_info.xres = info.width;
        var_info.yres = info.height;
        var_info.xres_virtual = info.width;
        var_info.yres_virtual = info.height;
        var_info.bits_per_pixel = info.bpp;
        /*
         * Default matches vbe_rgb_to_pixel() BGR888 + alpha in high byte
         * (common QEMU/PC framebuffer layout).
         */
        if (info.bpp == 32 || info.bpp == 24)
        {
            var_info.red.offset = 16;
            var_info.red.length = 8;
            var_info.green.offset = 8;
            var_info.green.length = 8;
            var_info.blue.offset = 0;
            var_info.blue.length = 8;
            if (info.bpp == 32)
            {
                var_info.transp.offset = 24;
                var_info.transp.length = 8;
            }
        }
        else if (info.bpp == 16)
        {
            var_info.red.offset = 11;
            var_info.red.length = 5;
            var_info.green.offset = 5;
            var_info.green.length = 6;
            var_info.blue.offset = 0;
            var_info.blue.length = 5;
        }
        if (copy_to_user(arg, &var_info, sizeof(var_info)) != 0)
            return -EFAULT;
        return 0;
    }
    /*
     * Fixed VBE/QEMU framebuffer: no mode switching. Accept PUT so TinyX
     * KdFindMode probes do not abort; subsequent FBIOGET_* report hardware.
     */
    if (request == FBIOPUT_VSCREENINFO && arg)
    {
        struct fb_var_screeninfo var;

        if (copy_from_user(&var, arg, sizeof(var)) != 0)
            return -EFAULT;
        (void)var;
        (void)info;
        return 0;
    }
    if (request == FBIOGET_FSCREENINFO && arg)
    {
        struct fb_fix_screeninfo fix;

        memset(&fix, 0, sizeof(fix));
        strncpy(fix.id, "IR0 FB", sizeof(fix.id) - 1);
        /*
         * IR0 mmap(/dev/fb0) maps fb_phys at offset 0 of the returned VA
         * (see sys_mmap fb path). TinyX then does
         *   fb = mmap_base + (smem_start % pagesize).
         * Reporting the raw physical address when it is not page-aligned
         * makes TinyX skip the first bytes and write past the mapping.
         * Use 0 so the userspace offset is always 0.
         */
        fix.smem_start = 0;
        fix.smem_len = info.fb_size;
        fix.type = FB_TYPE_PACKED_PIXELS;
        fix.visual = FB_VISUAL_TRUECOLOR;
        fix.line_length = info.pitch;
        fix.accel = FB_ACCEL_NONE;
        /* filled on VGET; here only fix */
        if (copy_to_user(arg, &fix, sizeof(fix)) != 0)
            return -EFAULT;
        return 0;
    }
    /* No hardware pan/blank — succeed so KDrive enable path continues. */
    if (request == FBIOPAN_DISPLAY || request == FBIOBLANK)
        return 0;
    /*
     * Colormap put/get: TrueColor FB ignores palette; accept so TinyX
     * fbdevUpdateFbColormap does not abort enable.
     */
    if (request == FBIOPUTCMAP || request == FBIOGETCMAP)
        return 0;
    return -EINVAL;
}

static const devfs_ops_t fb0_ops = {
    .read = dev_fb0_read,
    .write = dev_fb0_write_simple,
    .ioctl = dev_fb0_ioctl,
    .open = dev_fb0_open,
};

devfs_node_t dev_fb0 = {
    .entry = { .name = "fb0", .mode = 0660, .device_id = 15 },
    .ops = &fb0_ops,
    .ref_count = 0,
};
