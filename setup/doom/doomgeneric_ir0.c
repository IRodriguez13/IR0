/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * FASE55D - Real doomgeneric userspace backend for IR0.
 */

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "upstream/doomgeneric/doomgeneric.h"
#include "upstream/doomgeneric/doomkeys.h"

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

#define IR0_INPUT_IOCTL_GET_CAPS 0x49520002u

#define EV_KEY 0x01

#define IR0_KEY_ESC       1
#define IR0_KEY_ENTER     28
#define IR0_KEY_LEFTCTRL  29
#define IR0_KEY_A         30
#define IR0_KEY_D         32
#define IR0_KEY_Q         16
#define IR0_KEY_S         31
#define IR0_KEY_W         17
#define IR0_KEY_SPACE     57
#define IR0_KEY_LEFTSHIFT 42
#define IR0_KEY_UP        103
#define IR0_KEY_LEFT      105
#define IR0_KEY_RIGHT     106
#define IR0_KEY_DOWN      108

#define FASE55D_MAX_FRAMES 240
#define KEY_QUEUE_SIZE 64

#ifdef FASE55E_INTERACTIVE
#define DOOM_CFG_PATH "/etc/doom-frames"
#endif

struct input_event
{
    struct timeval time;
    uint16_t type;
    uint16_t code;
    int32_t value;
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
    uint32_t red;
    uint32_t green;
    uint32_t blue;
    uint32_t transp;
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

static int g_fd_fb = -1;
static int g_fd_input = -1;
static struct fb_var_screeninfo g_var;
static struct fb_fix_screeninfo g_fix;
static uint8_t *g_fb_map;
static size_t g_fb_len;
static uint16_t g_key_queue[KEY_QUEUE_SIZE];
static unsigned int g_key_write;
static unsigned int g_key_read;
static uint32_t g_frame_count;
static int g_first_frame_tagged;
#ifdef FASE55E_INTERACTIVE
static volatile int g_quit_requested;
static unsigned int g_frame_dump_every;
static int g_frame_dump_tagged;
#endif

static void write_str(const char *s)
{
    const char *p = s;

    while (*p)
    {
        p++;
    }
    (void)write(1, s, (size_t)(p - s));
}

static void write_fail(const char *step, const char *class_tag)
{
    write_str("[FASE55D][FAIL] step=");
    write_str(step ? step : "unknown");
    write_str("\n");
    if (class_tag && class_tag[0] != '\0')
    {
        write_str(class_tag);
        write_str("\n");
    }
}

static uint32_t ir0_monotonic_ms(void)
{
    struct timespec ts;

    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0)
    {
        return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
    }
    return 0;
}

static unsigned char convert_to_doom_key(uint16_t code)
{
    switch (code)
    {
        case IR0_KEY_ENTER:
            return KEY_ENTER;
        case IR0_KEY_ESC:
            return KEY_ESCAPE;
        case IR0_KEY_LEFT:
            return KEY_LEFTARROW;
        case IR0_KEY_RIGHT:
            return KEY_RIGHTARROW;
        case IR0_KEY_UP:
            return KEY_UPARROW;
        case IR0_KEY_DOWN:
            return KEY_DOWNARROW;
        case IR0_KEY_LEFTCTRL:
            return KEY_FIRE;
        case IR0_KEY_SPACE:
            return KEY_USE;
        case IR0_KEY_LEFTSHIFT:
            return KEY_RSHIFT;
        case IR0_KEY_W:
            return 'w';
        case IR0_KEY_A:
            return 'a';
        case IR0_KEY_S:
            return 's';
        case IR0_KEY_D:
            return 'd';
        case IR0_KEY_Q:
            return 'q';
        default:
            return 0;
    }
}

static void enqueue_key(int pressed, uint8_t doom_key)
{
    unsigned int next;
    uint16_t data;

    if (doom_key == 0)
    {
        return;
    }

    next = (g_key_write + 1u) % KEY_QUEUE_SIZE;
    if (next == g_key_read)
    {
        return;
    }

    data = (uint16_t)(((pressed ? 1u : 0u) << 8) | doom_key);
    g_key_queue[g_key_write] = data;
    g_key_write = next;
}

static void pump_input_events(void)
{
    struct input_event ev;

    if (g_fd_input < 0)
    {
        return;
    }

    for (;;)
    {
        ssize_t n = read(g_fd_input, &ev, sizeof(ev));

        if (n != (ssize_t)sizeof(ev))
        {
            break;
        }

        if (ev.type == EV_KEY)
        {
            unsigned char key = convert_to_doom_key(ev.code);

#ifdef FASE55E_INTERACTIVE
            if (ev.code == IR0_KEY_ESC && ev.value == 1)
            {
                g_quit_requested = 1;
            }
#endif
            if (ev.value == 1 || ev.value == 0)
            {
                enqueue_key(ev.value == 1, key);
            }
        }
    }
}

static void blit_framebuffer_scaled(void)
{
    uint32_t src_w = DOOMGENERIC_RESX;
    uint32_t src_h = DOOMGENERIC_RESY;
    uint32_t dst_w = g_var.xres;
    uint32_t dst_h = g_var.yres;
    uint32_t bpp = g_var.bits_per_pixel / 8;
    uint32_t x;
    uint32_t y;
    const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;

    if (!g_fb_map || bpp == 0 || bpp > 4)
    {
        return;
    }

    if (dst_w == src_w && dst_h == src_h && bpp == 4)
    {
        for (y = 0; y < src_h; y++)
        {
            uint8_t *dst_row = g_fb_map + ((size_t)y * g_fix.line_length);
            const uint8_t *src_row = (const uint8_t *)&src[(size_t)y * src_w];
            memcpy(dst_row, src_row, (size_t)src_w * 4u);
        }
        return;
    }

    for (y = 0; y < dst_h; y++)
    {
        uint32_t sy = (uint32_t)(((uint64_t)y * src_h) / dst_h);
        for (x = 0; x < dst_w; x++)
        {
            uint32_t sx = (uint32_t)(((uint64_t)x * src_w) / dst_w);
            uint32_t c = src[(size_t)sy * src_w + sx];
            uint8_t *dst = g_fb_map + ((size_t)y * g_fix.line_length) + ((size_t)x * bpp);

            if (bpp == 4)
            {
                memcpy(dst, &c, 4);
            }
            else if (bpp == 3)
            {
                dst[0] = (uint8_t)(c & 0xFFu);
                dst[1] = (uint8_t)((c >> 8) & 0xFFu);
                dst[2] = (uint8_t)((c >> 16) & 0xFFu);
            }
            else if (bpp == 2)
            {
                uint16_t rgb565 =
                    (uint16_t)((((c >> 19) & 0x1Fu) << 11) |
                               (((c >> 10) & 0x3Fu) << 5) |
                               (((c >> 3) & 0x1Fu)));
                memcpy(dst, &rgb565, 2);
            }
            else
            {
                dst[0] = (uint8_t)(c & 0xFFu);
            }
        }
    }
}

void DG_Init(void)
{
    struct ir0_input_caps caps;

    memset(g_key_queue, 0, sizeof(g_key_queue));
    memset(&g_var, 0, sizeof(g_var));
    memset(&g_fix, 0, sizeof(g_fix));

    g_fd_fb = open("/dev/fb0", O_RDWR);
    if (g_fd_fb < 0)
    {
        write_fail("open_fb0", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    if (ioctl(g_fd_fb, FBIOGET_VSCREENINFO, &g_var) != 0 ||
        ioctl(g_fd_fb, FBIOGET_FSCREENINFO, &g_fix) != 0)
    {
        write_fail("fb_ioctl", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    g_fb_len = (size_t)g_fix.smem_len;
    if (g_fb_len < 4096)
    {
        g_fb_len = 4096;
    }

    g_fb_map = (uint8_t *)mmap(NULL, g_fb_len, PROT_READ | PROT_WRITE, MAP_SHARED, g_fd_fb, 0);
    if (g_fb_map == MAP_FAILED)
    {
        write_fail("fb_mmap", "FB_SUBSYSTEM_FAIL");
        exit(1);
    }

    g_fd_input = open("/dev/events0", O_RDONLY | O_NONBLOCK);
    if (g_fd_input < 0)
    {
        write_fail("open_events0", "INPUT_SUBSYSTEM_FAIL");
        exit(1);
    }

    memset(&caps, 0, sizeof(caps));
    if (ioctl(g_fd_input, IR0_INPUT_IOCTL_GET_CAPS, &caps) != 0)
    {
        write_fail("events0_get_caps", "INPUT_SUBSYSTEM_FAIL");
        exit(1);
    }

    write_str("DOOMGENERIC_INIT_OK\n");
}

void DG_DrawFrame(void)
{
    g_frame_count++;
    blit_framebuffer_scaled();
    if (!g_first_frame_tagged)
    {
        g_first_frame_tagged = 1;
        write_str("DOOMGENERIC_FIRST_FRAME_OK\n");
    }
}

void DG_SleepMs(uint32_t ms)
{
    struct timespec req;

    req.tv_sec = (time_t)(ms / 1000u);
    req.tv_nsec = (long)((ms % 1000u) * 1000000u);
    if (nanosleep(&req, NULL) != 0 && errno == ENOSYS)
    {
        write_fail("nanosleep", "TIMING_SUBSYSTEM_FAIL");
        exit(1);
    }
}

uint32_t DG_GetTicksMs(void)
{
    uint32_t t = ir0_monotonic_ms();

    if (t == 0)
    {
        write_fail("clock_gettime", "TIMING_SUBSYSTEM_FAIL");
        exit(1);
    }
    return t;
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
    uint16_t k;

    if (!pressed || !doomKey)
    {
        return 0;
    }

    pump_input_events();
    if (g_key_read == g_key_write)
    {
        return 0;
    }

    k = g_key_queue[g_key_read];
    g_key_read = (g_key_read + 1u) % KEY_QUEUE_SIZE;
    *pressed = (k >> 8) & 0x1;
    *doomKey = (unsigned char)(k & 0xFFu);
    return 1;
}

void DG_SetWindowTitle(const char *title)
{
    (void)title;
}

static int check_required_wad(void)
{
    int fd = open("/usr/share/doom/doom1.wad", O_RDONLY);
    uint8_t hdr[12];
    ssize_t n;

    if (fd < 0)
    {
        write_fail("open_wad", "ABI_MISSING");
        return -1;
    }

    n = read(fd, hdr, sizeof(hdr));
    close(fd);
    if (n != (ssize_t)sizeof(hdr))
    {
        write_fail("read_wad_header", "ABI_MISSING");
        return -1;
    }

    if (memcmp(hdr, "IWAD", 4) != 0 && memcmp(hdr, "PWAD", 4) != 0)
    {
        write_fail("wad_magic", "ABI_MISSING");
        return -1;
    }

    write_str("DOOMGENERIC_WAD_LOAD_OK\n");
    return 0;
}

#ifdef FASE55E_INTERACTIVE
static unsigned int read_cfg_line(FILE *f, unsigned int default_val)
{
    char buf[32];
    unsigned long val;

    if (!fgets(buf, sizeof(buf), f))
    {
        return default_val;
    }
    val = strtoul(buf, NULL, 10);
    if (val > 0xFFFFFFFFu)
    {
        return default_val;
    }
    return (unsigned int)val;
}

static void read_interactive_cfg(unsigned int *frame_limit, unsigned int *dump_every)
{
    FILE *f;

    *frame_limit = 0;
    *dump_every = 0;

    f = fopen(DOOM_CFG_PATH, "r");
    if (!f)
    {
        return;
    }
    *frame_limit = read_cfg_line(f, 0);
    *dump_every = read_cfg_line(f, 0);
    fclose(f);
}

static void dump_framebuffer_ppm(unsigned int frame_num)
{
    char path[64];
    FILE *f;
    uint32_t x;
    uint32_t y;
    const uint32_t *src = (const uint32_t *)DG_ScreenBuffer;
    int fd;

    snprintf(path, sizeof(path), "/tmp/doom-frame-%05u.ppm", frame_num);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
    {
        return;
    }

    f = fdopen(fd, "w");
    if (!f)
    {
        close(fd);
        return;
    }

    fprintf(f, "P6\n%u %u\n255\n", DOOMGENERIC_RESX, DOOMGENERIC_RESY);
    for (y = 0; y < DOOMGENERIC_RESY; y++)
    {
        for (x = 0; x < DOOMGENERIC_RESX; x++)
        {
            uint32_t c = src[(size_t)y * DOOMGENERIC_RESX + x];
            uint8_t rgb[3];

            rgb[0] = (uint8_t)(c >> 16);
            rgb[1] = (uint8_t)(c >> 8);
            rgb[2] = (uint8_t)(c);
            if (fwrite(rgb, 1, 3, f) != 3)
            {
                break;
            }
        }
    }
    fclose(f);

    if (!g_frame_dump_tagged)
    {
        g_frame_dump_tagged = 1;
        write_str("DOOMGENERIC_FRAME_DUMP_OK\n");
    }
}

static int run_interactive_loop(unsigned int frame_limit)
{
    write_str("DOOMGENERIC_INPUT_MANUAL_READY\n");
    write_str("DOOMGENERIC_INTERACTIVE_READY\n");

    while (!g_quit_requested)
    {
        if (frame_limit > 0 && g_frame_count >= frame_limit)
        {
            break;
        }

        doomgeneric_Tick();

        if (g_first_frame_tagged)
        {
            static int visible_tagged;

            if (!visible_tagged)
            {
                visible_tagged = 1;
                write_str("DOOMGENERIC_FRAMEBUFFER_VISIBLE\n");
            }
        }

        if (g_frame_dump_every > 0 && g_frame_count > 0 &&
            (g_frame_count % g_frame_dump_every) == 0)
        {
            dump_framebuffer_ppm(g_frame_count);
        }
    }

    write_str("DOOMGENERIC_INTERACTIVE_EXIT_OK\n");
    return 0;
}
#endif

int main(void)
{
    char *argv[] = {
        (char *)"doomgeneric-ir0",
        (char *)"-iwad",
        (char *)"/usr/share/doom/doom1.wad",
        (char *)"-nomusic",
        (char *)"-nosound",
        NULL
    };

    write_str("DOOMGENERIC_BUILD_OK\n");
#ifdef FASE55E_INTERACTIVE
    write_str("DOOMGENERIC_BUILD_MODE=interactive\n");
#else
    write_str("DOOMGENERIC_BUILD_MODE=smoke\n");
#endif

    if (check_required_wad() != 0)
    {
        return 1;
    }

    doomgeneric_Create(5, argv);
    write_str("DOOMGENERIC_INIT_OK\n");

#ifdef FASE55E_INTERACTIVE
    {
        unsigned int frame_limit;
        unsigned int dump_every;

        read_interactive_cfg(&frame_limit, &dump_every);
        g_frame_dump_every = dump_every;
        run_interactive_loop(frame_limit);
    }
#else
    {
        uint32_t start_ms;
        uint32_t end_ms;

        start_ms = DG_GetTicksMs();
        while (g_frame_count < FASE55D_MAX_FRAMES)
        {
            doomgeneric_Tick();
        }
        end_ms = DG_GetTicksMs();

		write_str("DOOMGENERIC_FRAME_LOOP_OK\n");
		if (end_ms > start_ms && (end_ms - start_ms) > 60000u)
		{
			write_str("LONG_RUNNING_BUT_STABLE\n");
		}
		write_str("FASE55D_DOOMGENERIC_OK\n");
		/* Optional KTM product case (inline ioctl — avoid kernel -Iincludes). */
		{
			int kfd = open("/dev/ktm", O_RDWR);
			if (kfd >= 0)
			{
				/* KTM_IOC_USER_EVENT = 0x4B07; CASE_BEGIN=21 CASE_END=22 */
				struct
				{
					unsigned type;
					unsigned subsystem;
					unsigned long long arg0, arg1, arg2, arg3;
					char name[64];
				} ev;
				memset(&ev, 0, sizeof(ev));
				ev.type = 21u;
				ev.subsystem = 7u;
				memcpy(ev.name, "doomgeneric_55d", 16);
				(void)ioctl(kfd, 0x4B07u, &ev);
				ev.type = 22u;
				ev.arg0 = 0;
				(void)ioctl(kfd, 0x4B07u, &ev);
				(void)close(kfd);
				write_str("KTM_DOOM_55D_OK\n");
			}
			else
				write_str("KTM_DOOM_55D_SKIP\n");
		}
	}
#endif

    if (g_fd_input >= 0)
    {
        close(g_fd_input);
    }
    if (g_fb_map && g_fb_map != MAP_FAILED)
    {
        (void)munmap(g_fb_map, g_fb_len);
    }
    if (g_fd_fb >= 0)
    {
        close(g_fd_fb);
    }

#ifdef FASE55E_INTERACTIVE
    for (;;)
    {
        (void)pause();
    }
#endif

    return 0;
}
