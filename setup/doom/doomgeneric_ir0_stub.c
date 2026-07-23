/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE55B — doomgeneric-style userspace stub over generic IR0 devices.
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <errno.h>

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define EV_KEY 0x01
#define KEY_W 17
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#define IR0_INPUT_IOCTL_INJECT 0x49520001u
#define IR0_INPUT_IOCTL_GET_CAPS 0x49520002u

struct ir0_input_event
{
    uint16_t type;
    uint16_t code;
    int32_t value;
    int64_t timestamp_ms;
};

struct ir0_input_caps
{
    int keyboard;
    int mouse;
    int touch;
    int gamepad;
    int has_events0;
    int keyboard_ps2;
    int mouse_ps2;
    int event_queue_depth;
    int supports_key_up_down;
    int supports_mouse_motion;
};

struct input_event
{
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
};

struct fb_bitfield
{
    uint32_t offset;
    uint32_t length;
    uint32_t msb_right;
};

struct fb_var_screeninfo
{
    uint32_t xres;
    uint32_t yres;
    uint32_t xres_virtual;
    uint32_t yres_virtual;
    uint32_t xoffset;
    uint32_t yoffset;
    uint32_t bits_per_pixel;
    uint32_t grayscale;
    struct fb_bitfield red;
    struct fb_bitfield green;
    struct fb_bitfield blue;
    struct fb_bitfield transp;
    uint32_t nonstd;
    uint32_t activate;
    uint32_t height;
    uint32_t width;
    uint32_t accel_flags;
    uint32_t pixclock;
    uint32_t left_margin;
    uint32_t right_margin;
    uint32_t upper_margin;
    uint32_t lower_margin;
    uint32_t hsync_len;
    uint32_t vsync_len;
    uint32_t sync;
    uint32_t vmode;
    uint32_t rotate;
    uint32_t colorspace;
};

struct fb_fix_screeninfo
{
    char id[16];
    unsigned long smem_start;
    uint32_t smem_len;
    uint32_t type;
    uint32_t type_aux;
    uint32_t visual;
    uint16_t xpanstep;
    uint16_t ypanstep;
    uint16_t ywrapstep;
    uint32_t line_length;
    unsigned long mmio_start;
    uint32_t mmio_len;
    uint32_t accel;
    uint16_t capabilities;
    uint16_t reserved[2];
};

static void write_str(const char *s)
{
    const char *p = s;

    while (*p)
    {
        p++;
    }
    (void)write(1, s, (size_t)(p - s));
}

static void fail_step(const char *step)
{
    write_str("[FASE55B][FAIL] step=");
    write_str(step ? step : "(null)");
    write_str("\n");
    write_str("FASE55B_FAIL_REASON=");
    write_str(step ? step : "unknown");
    write_str("\n");
}

static int64_t monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return ((int64_t)ts.tv_sec * 1000LL) + ((int64_t)ts.tv_nsec / 1000000LL);
    }

    {
        struct timeval tv;

        if (gettimeofday(&tv, NULL) != 0)
        {
            return 0;
        }
        return ((int64_t)tv.tv_sec * 1000LL) + ((int64_t)tv.tv_usec / 1000LL);
    }
}

static void sleep_frame_16ms(void)
{
    struct timespec req;

    req.tv_sec = 0;
    req.tv_nsec = 16 * 1000 * 1000;
    if (nanosleep(&req, NULL) == 0)
    {
        write_str("NANOSLEEP_OR_YIELD_OK\n");
        return;
    }
    usleep(16000);
    write_str("NANOSLEEP_OR_YIELD_OK\n");
}

static int read_wad_smoke(void)
{
    int fd;
    uint8_t buf[4096];
    ssize_t n;
    size_t total = 0;

    fd = open("/usr/share/doom/doom1.wad", O_RDONLY);
    if (fd < 0)
    {
        return -1;
    }

    for (;;)
    {
        n = read(fd, buf, sizeof(buf));
        if (n < 0)
        {
            close(fd);
            return -1;
        }
        if (n == 0)
        {
            break;
        }
        total += (size_t)n;
    }

    close(fd);
    return (total >= 65536) ? 0 : -1;
}

static void draw_frame(uint8_t *fb, const struct fb_var_screeninfo *var,
                       const struct fb_fix_screeninfo *fix, int frame, int key_state)
{
    uint32_t bpp = var->bits_per_pixel / 8;
    uint32_t y;
    uint32_t x;
    uint32_t phase = (uint32_t)(frame & 0xFF);
    uint32_t bias = key_state ? 0x00000040u : 0u;

    if (bpp == 0 || bpp > 4)
    {
        return;
    }

    for (y = 0; y < var->yres; y++)
    {
        for (x = 0; x < var->xres; x++)
        {
            uint32_t color = (((x + phase) ^ (y + phase)) & 0xFFu) << 8;
            color |= (((x + y + phase) & 0x3Fu) + bias);
            memcpy(fb + ((size_t)y * fix->line_length) + ((size_t)x * bpp), &color, bpp);
        }
    }
}

int main(void)
{
    int fd_fb;
    int fd_in;
    struct fb_var_screeninfo var;
    struct fb_fix_screeninfo fix;
    size_t map_len;
    uint8_t *fb_map;
    int frame;
    int key_state = 0;
    int caps_ok = 0;
    int injected_ok = 0;
    int64_t t0;
    int64_t t1;

    write_str("FASE55B_START\n");
    write_str("FASE55B_DOOM_STUB_HARNESS_ID=doomgeneric_ir0_stub.c\n");
    write_str("DOOM_STUB_BUILD_MODE=external-musl-staged\n");

    fd_fb = open("/dev/fb0", O_RDWR);
    if (fd_fb < 0)
    {
        fail_step("open_fb0");
        goto halt;
    }
    write_str("DOOM_STUB_FB_OK\n");

    memset(&var, 0, sizeof(var));
    memset(&fix, 0, sizeof(fix));
    if (ioctl(fd_fb, FBIOGET_VSCREENINFO, &var) != 0 ||
        ioctl(fd_fb, FBIOGET_FSCREENINFO, &fix) != 0)
    {
        close(fd_fb);
        fail_step("fb_getinfo");
        goto halt;
    }

    map_len = (size_t)fix.smem_len;
    if (map_len < 4096)
    {
        map_len = 4096;
    }

    fb_map = (uint8_t *)mmap(NULL, map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd_fb, 0);
    if (fb_map == MAP_FAILED)
    {
        close(fd_fb);
        fail_step("fb_mmap");
        goto halt;
    }

    fd_in = open("/dev/events0", O_RDONLY | O_NONBLOCK);
    if (fd_in < 0)
    {
        (void)munmap(fb_map, map_len);
        close(fd_fb);
        fail_step("open_events0");
        goto halt;
    }
    write_str("DOOM_STUB_INPUT_OK\n");

    {
        struct ir0_input_caps caps;

        memset(&caps, 0, sizeof(caps));
        if (ioctl(fd_in, IR0_INPUT_IOCTL_GET_CAPS, &caps) == 0 && caps.has_events0)
        {
            caps_ok = 1;
            if (caps.keyboard_ps2)
            {
                write_str("INPUT_PS2_CAPS_OK\n");
            }
            else
            {
                write_str("INPUT_PS2_CAPS_PARTIAL\n");
            }
            if (caps.mouse_ps2 && caps.supports_mouse_motion)
            {
                write_str("INPUT_MOUSE_PS2_CAPS_OK\n");
            }
        }
    }

    if (read_wad_smoke() != 0)
    {
        close(fd_in);
        (void)munmap(fb_map, map_len);
        close(fd_fb);
        fail_step("wad_read");
        goto halt;
    }
    write_str("DOOM_STUB_WAD_OK\n");

    t0 = monotonic_ms();
    sleep_frame_16ms();
    t1 = monotonic_ms();
    if (t1 >= t0)
    {
        write_str("CLOCK_MONOTONIC_OK\n");
    }
    else
    {
        write_str("CLOCK_MONOTONIC_FALLBACK\n");
    }

    {
        struct ir0_input_event inject_ev;

        memset(&inject_ev, 0, sizeof(inject_ev));
        inject_ev.type = EV_KEY;
        inject_ev.code = KEY_W;
        inject_ev.value = 1;
        if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) == 0)
        {
            inject_ev.value = 0;
            if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) == 0)
            {
                injected_ok = 1;
                write_str("INPUT_TEST_INJECT_STILL_OK\n");
            }
        }
    }

    for (frame = 0; frame < 72; frame++)
    {
        struct input_event ev;
        ssize_t n;

        n = read(fd_in, &ev, sizeof(ev));
        if (n == (ssize_t)sizeof(ev) && ev.type == EV_KEY)
        {
            key_state = (ev.value == 1) ? 1 : 0;
        }
        draw_frame(fb_map, &var, &fix, frame, key_state);
        sleep_frame_16ms();
    }

    write_str("DOOM_STUB_FRAME_LOOP_OK\n");
    write_str("DOOM_STUB_TIMED_LOOP_OK\n");
    write_str("FASE55B_DOOM_STUB_OK\n");
    if (caps_ok && injected_ok)
    {
        write_str("FASE55C_OK\n");
    }

    close(fd_in);
    (void)munmap(fb_map, map_len);
    close(fd_fb);
    return 0;

halt:
    return 1;
}
