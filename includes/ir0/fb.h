/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — framebuffer facade for /dev/fb0 and userspace bridges.
 */

#ifndef _IR0_FB_H
#define _IR0_FB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

struct ir0_fb_info
{
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t fb_size;
    uint32_t fb_phys;
};

bool ir0_fb_is_available(void);
bool ir0_fb_get_info(struct ir0_fb_info *info);
int64_t ir0_fb_write_bytes(size_t offset, const void *buf, size_t count);
int ir0_fb_write_rect(uint32_t x, uint32_t y, uint32_t w, uint32_t h,
                      uint32_t pixel);

#endif /* _IR0_FB_H */
