/**
 * IR0 userspace — KTM X11/Xfbdev prep (evdev + fb bitfields)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_x11_evdev_fb_case.c
 * Description: EVIOCGVERSION/EVIOCGBIT, /dev/input/event0, SYN_REPORT inject,
 *              FBIOGET_VSCREENINFO Linux color bitfields.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "libktm_user.h"

#define FBIOGET_VSCREENINFO 0x4600

#define EV_SYN 0x00
#define EV_KEY 0x01
#define EV_REL 0x02
#define SYN_REPORT 0
#define REL_X 0x00
#define KEY_W 17

#define IR0_INPUT_IOCTL_INJECT 0x49520001u
#define EV_VERSION 0x010001
#define EVIOCGVERSION 0x80044501u

/* EVIOCGBIT(ev, 8) — _IOC(_IOC_READ,'E',0x20+ev,8) */
#define EVIOCGBIT8(ev) (0x80084520u + (unsigned)(ev))

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

struct ir0_input_event
{
	uint16_t type;
	uint16_t code;
	int32_t value;
	int64_t timestamp_ms;
};

struct input_event
{
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int bit_test(const unsigned long *bits, unsigned int bit)
{
	return (bits[bit / (8u * sizeof(unsigned long))] >>
		(bit % (8u * sizeof(unsigned long)))) &
	       1UL;
}

static int test_fb_bitfields(void)
{
	int fd;
	struct fb_var_screeninfo var;

	fd = open("/dev/fb0", O_RDWR);
	if (fd < 0)
		return -1;
	memset(&var, 0, sizeof(var));
	if (ioctl(fd, FBIOGET_VSCREENINFO, &var) != 0)
	{
		close(fd);
		return -1;
	}
	close(fd);
	if (var.bits_per_pixel == 0 || var.xres == 0 || var.yres == 0)
		return -1;
	if (var.bits_per_pixel >= 24)
	{
		if (var.red.length == 0 || var.green.length == 0 ||
		    var.blue.length == 0)
			return -1;
	}
	say("FB_BITFIELD_OK\n");
	return 0;
}

static int test_evdev_ioctls(const char *path)
{
	int fd;
	int version = 0;
	unsigned long bits[8];
	struct ir0_input_event inj;
	struct input_event ev;
	int saw_rel = 0;
	int saw_syn = 0;
	int i;

	fd = open(path, O_RDWR | O_NONBLOCK);
	if (fd < 0)
		return -1;

	if (ioctl(fd, EVIOCGVERSION, &version) != 0 || version != EV_VERSION)
	{
		close(fd);
		return -1;
	}
	say("EVIOCGVERSION_OK\n");

	memset(bits, 0, sizeof(bits));
	if (ioctl(fd, EVIOCGBIT8(0), bits) != 0)
	{
		close(fd);
		return -1;
	}
	if (!bit_test(bits, EV_KEY) || !bit_test(bits, EV_REL) ||
	    !bit_test(bits, EV_SYN))
	{
		close(fd);
		return -1;
	}
	say("EVIOCGBIT_OK\n");

	memset(&inj, 0, sizeof(inj));
	inj.type = EV_REL;
	inj.code = REL_X;
	inj.value = 3;
	if (ioctl(fd, IR0_INPUT_IOCTL_INJECT, &inj) != 0)
	{
		close(fd);
		return -1;
	}

	for (i = 0; i < 16; i++)
	{
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n != (ssize_t)sizeof(ev))
			break;
		if (ev.type == EV_REL && ev.code == REL_X && ev.value == 3)
			saw_rel = 1;
		if (ev.type == EV_SYN && ev.code == SYN_REPORT)
			saw_syn = 1;
	}
	close(fd);
	if (!saw_rel || !saw_syn)
		return -1;
	say("SYN_REPORT_OK\n");
	return 0;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;
	struct stat st;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_X11_EVDEV_FB_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_X11_EVDEV_FB_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "x11_evdev_fb") != 0)
	{
		say("KTM_X11_EVDEV_FB_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (test_fb_bitfields() != 0)
	{
		(void)ktm_assert_true(kfd, "fb_bitfields", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "fb_bitfields", 1);

	if (stat("/dev/input/event0", &st) != 0)
	{
		(void)ktm_assert_true(kfd, "input_event0_path", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "input_event0_path", 1);
		say("INPUT_EVENT0_PATH_OK\n");
	}

	if (test_evdev_ioctls("/dev/events0") != 0)
	{
		(void)ktm_assert_true(kfd, "evdev_events0", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "evdev_events0", 1);

	if (test_evdev_ioctls("/dev/input/event0") != 0)
	{
		(void)ktm_assert_true(kfd, "evdev_input_event0", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(kfd, "evdev_input_event0", 1);

	(void)ktm_case_end(kfd, "x11_evdev_fb", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	if (fails == 0)
	{
		say("KTM_X11_EVDEV_FB_OK\n");
		say("KTM_USERDEV_OK\n");
	}
	else
		say("KTM_X11_EVDEV_FB_FAIL\n");
	return fails == 0 ? 0 : 1;
}
