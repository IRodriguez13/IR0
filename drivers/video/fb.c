/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — framebuffer facade for /dev/fb0 and userspace bridges.
 */

#include <ir0/fb.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>
#include <ir0/video_backend.h>
#include <string.h>

#define IR0_FB_VGA_TEXT_PHYS 0x000B8000u

static bool ir0_fb_fill_info(struct ir0_fb_info *info)
{
    uint32_t width;
    uint32_t height;
    uint32_t bpp;

    if (!info)
        return false;
    if (!video_backend_is_available())
        return false;
    if (!video_backend_get_info(&width, &height, &bpp))
        return false;

    info->width = width;
    info->height = height;
    info->bpp = bpp;
    info->pitch = video_backend_get_pitch();
    info->fb_phys = video_backend_get_fb_phys();
    info->fb_size = video_backend_get_fb_size();
    return true;
}

bool ir0_fb_is_available(void)
{
    struct ir0_fb_info info;

    if (!ir0_fb_fill_info(&info))
        return false;

    /*
     * Treat VGA text fallback as unavailable for fbdev consumers.
     * This keeps /dev/fb0 semantics aligned with linear framebuffer clients.
     */
    if (info.fb_phys == IR0_FB_VGA_TEXT_PHYS)
        return false;
    if (info.width < 320 || info.height < 200)
        return false;
    if (info.pitch == 0 || info.fb_size == 0)
        return false;

    return true;
}

bool ir0_fb_get_info(struct ir0_fb_info *info)
{
    if (!info)
        return false;
    if (!ir0_fb_is_available())
        return false;

    return ir0_fb_fill_info(info);
}

int64_t ir0_fb_write_bytes(size_t offset, const void *buf, size_t count)
{
    struct ir0_fb_info info;
    uint8_t *fb;

    if (!buf)
        return -EFAULT;
    if (!ir0_fb_get_info(&info))
        return -ENODEV;
    if (offset >= info.fb_size)
        return 0;

    fb = video_backend_get_fb();
    if (!fb)
        return -ENODEV;

    if ((uint64_t)count > info.fb_size - offset)
        count = (size_t)(info.fb_size - offset);
    memcpy(fb + offset, buf, count);
    return (int64_t)count;
}

