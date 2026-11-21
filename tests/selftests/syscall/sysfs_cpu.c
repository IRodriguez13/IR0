/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sysfs_cpu.c
 * Description: Read /sys/devices/system/cpu/cpu0/online and check the flag byte.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"

#ifndef IR0_SELFTEST_NOLIBC
#include <fcntl.h>
#include <unistd.h>
#endif

#define SYSFS_CPU0_ONLINE "/sys/devices/system/cpu/cpu0/online"

int main(void)
{
	char buf[8];
	int fd;
	ssize_t n;
	int pass;

	ksft_set_plan(1);

	fd = open(SYSFS_CPU0_ONLINE, O_RDONLY);
	if (fd < 0)
	{
		ksft_test_result(0, "cpu0_online");
		return ksft_exit();
	}

	n = read(fd, buf, sizeof(buf));
	(void)close(fd);
	pass = (n > 0 && (buf[0] == '0' || buf[0] == '1'));
	ksft_test_result(pass, "cpu0_online");
	return ksft_exit();
}
