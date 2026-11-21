/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev_abs.c
 * Description: EVIOCGABS fails with EINVAL: the event node has no absolute axes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"

static int get_abs(int fd, void *info)
{
	return (int)syscall(29, fd, 0x80184540L, (long)info, 0);
}

static int is_einval(int rc)
{
	return rc < 0 && errno == 22;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

static int get_abs(int fd, void *info)
{
	return ioctl(fd, 0x80184540u, info);
}

static int is_einval(int rc)
{
	return rc < 0 && errno == EINVAL;
}
#endif

#include "ir0/ksft.h"

int main(void)
{
	char info[24];
	int fd;
	int pass;

	ksft_set_plan(1);
	fd = open("/dev/input/event0", 0);
	if (fd < 0)
	{
		ksft_test_result(0, "evdev_abs");
		return ksft_exit();
	}
	pass = is_einval(get_abs(fd, info));
	(void)close(fd);
	ksft_test_result(pass, "evdev_abs");
	return ksft_exit();
}
