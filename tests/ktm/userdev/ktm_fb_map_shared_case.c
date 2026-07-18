/**
 * IR0 userspace — KTM MAP_SHARED /dev/fb0 case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fb_map_shared_case.c
 * Description: mmap MAP_SHARED on framebuffer (X11/fbdev prep gate).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "libktm_user.h"

#define FBIOGET_VSCREENINFO 0x4600
#define FBIOGET_FSCREENINFO 0x4602

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

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_fb_map_shared(void)
{
	int fd;
	struct fb_fix_screeninfo fix;
	struct fb_var_screeninfo var;
	size_t map_len;
	unsigned char *map;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0)
		return -1;
	memset(&fix, 0, sizeof(fix));
	memset(&var, 0, sizeof(var));
	if (ioctl(fd, FBIOGET_FSCREENINFO, &fix) != 0)
		goto fail_fd;
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
		goto fail_fd;
	map_len = (size_t)fix.smem_len;
	if (map_len > 4096u)
		map_len = 4096u;
	if (map_len < 4u)
		goto fail_fd;

	map = (unsigned char *)mmap(NULL, map_len, PROT_READ | PROT_WRITE,
				    MAP_SHARED, fd, 0);
	if (map == MAP_FAILED)
		goto fail_fd;

	map[0] = 0xAA;
	map[1] = 0xBB;
	map[2] = 0xCC;
	map[3] = 0xDD;

	(void)munmap(map, map_len);
	close(fd);
	(void)var;
	return 0;

fail_fd:
	close(fd);
	return -1;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_FB_MAP_SHARED_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_FB_MAP_SHARED_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "fb_map_shared") != 0)
	{
		say("KTM_FB_MAP_SHARED_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (test_fb_map_shared() != 0)
	{
		(void)ktm_assert_true(kfd, "fb_map_shared", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "fb_map_shared", 1);
		say("FB_MAP_SHARED_USER_OK\n");
		say("KTM_FB_MAP_SHARED_OK\n");
	}

	(void)ktm_case_end(kfd, "fb_map_shared", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	if (fails == 0)
		say("KTM_USERDEV_OK\n");
	else
		say("KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
