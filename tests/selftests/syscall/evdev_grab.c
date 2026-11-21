/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev_grab.c
 * Description: EVIOCGRAB is exclusive to one open of the event device.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"

static int ioctl_grab(int fd, int grab)
{
	return (int)syscall(29, fd, 0x40044590L, (long)grab, 0);
}
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define EVIOCGRAB 0x40044590u

static int ioctl_grab(int fd, int grab)
{
	return ioctl(fd, EVIOCGRAB, grab);
}
#endif

#include "ir0/ksft.h"

static int is_busy(void)
{
#ifdef IR0_SELFTEST_NOLIBC
	return errno == 16;
#else
	return errno == EBUSY;
#endif
}

int main(void)
{
	int a;
	int b;
	int pass;

	ksft_set_plan(1);
	a = open("/dev/input/event0", 0);
	b = open("/dev/input/event0", 0);
	if (a < 0 || b < 0)
	{
		ksft_test_result(0, "evdev_grab");
		return ksft_exit();
	}
	pass = ioctl_grab(a, 1) == 0 &&
	       ioctl_grab(b, 1) < 0 && is_busy() &&
	       ioctl_grab(a, 0) == 0 &&
	       ioctl_grab(b, 1) == 0;
	ksft_test_result(pass, "evdev_grab");
	return ksft_exit();
}
