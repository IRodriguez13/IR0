/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev_clock.c
 * Description: EVIOCSCLOCKID accepts realtime and monotonic only.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"

static int set_clock(int fd, int clockid)
{
	return (int)syscall(29, fd, 0x400445a0L, (long)&clockid, 0);
}
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int set_clock(int fd, int clockid)
{
	return ioctl(fd, 0x400445a0u, &clockid);
}
#endif

#include "ir0/ksft.h"

int main(void)
{
	int fd;
	int pass;

	ksft_set_plan(1);
	fd = open("/dev/input/event0", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "evdev_clock");
		return ksft_exit();
	}
	/* 1 is CLOCK_MONOTONIC, 0 is CLOCK_REALTIME, 7 is not a clock. */
	pass = set_clock(fd, 1) == 0 && set_clock(fd, 7) < 0 &&
	       set_clock(fd, 0) == 0;
	(void)close(fd);
	ksft_test_result(pass, "evdev_clock");
	return ksft_exit();
}
