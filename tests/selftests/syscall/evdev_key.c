/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev_key.c
 * Description: EVIOCGKEY reports KEY_A down after an injected EV_KEY.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#include <stdint.h>

static int evdev_ioctl(int fd, unsigned long req, void *arg)
{
	return (int)syscall(29, fd, (long)req, (long)arg, 0);
}
#else
#include <stdint.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int evdev_ioctl(int fd, unsigned long req, void *arg)
{
	return ioctl(fd, req, arg);
}
#endif

#include "ir0/ksft.h"

/* includes/ir0/input.h — inject argument, not a Linux evdev struct. */
struct ir0_input_event
{
	uint16_t type;
	uint16_t code;
	int32_t value;
	int64_t timestamp_ms;
};

int main(void)
{
	struct ir0_input_event inj;
	unsigned char keys[32];
	unsigned int i;
	int fd;
	int pass;
	int rc;

	ksft_set_plan(1);
	pass = 0;
	fd = open("/dev/input/event0", 0);
	if (fd >= 0)
	{
		inj.type = 1;
		inj.code = 30;
		inj.value = 1;
		inj.timestamp_ms = 0;
		for (i = 0; i < sizeof(keys); i++)
		{
			keys[i] = 0;
		}
		if (evdev_ioctl(fd, 0x49520001u, &inj) == 0)
		{
			rc = evdev_ioctl(fd, 0x80204518u, keys);
			pass = rc == 0 &&
			       (keys[30u / 8u] & (1u << (30u % 8u))) != 0;
		}
		(void)close(fd);
	}
	ksft_test_result(pass, "evdev_key");
	return ksft_exit();
}
