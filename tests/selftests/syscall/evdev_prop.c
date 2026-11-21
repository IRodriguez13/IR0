/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev_prop.c
 * Description: EVIOCGPROP reports INPUT_PROP_POINTER for the event node.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"

static int ioctl_prop(int fd, unsigned int *bits)
{
	return (int)syscall(29, fd, 0x80044509L, (long)bits, 0);
}
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define EVIOCGPROP4 0x80044509u

static int ioctl_prop(int fd, unsigned int *bits)
{
	return ioctl(fd, EVIOCGPROP4, bits);
}
#endif

#include "ir0/ksft.h"

int main(void)
{
	unsigned int bits;
	volatile unsigned char *zero;
	unsigned int i;
	int fd;
	int pass;

	ksft_set_plan(1);
	zero = (volatile unsigned char *)&bits;
	for (i = 0; i < sizeof(bits); i++)
		zero[i] = 0;

	fd = open("/dev/input/event0", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "evdev_prop");
		return ksft_exit();
	}
	pass = ioctl_prop(fd, &bits) == 0 && (bits & 1u) != 0;
	ksft_test_result(pass, "evdev_prop");
	return ksft_exit();
}
