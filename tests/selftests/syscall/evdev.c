/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: evdev.c
 * Description: /dev/input/event0 per-open rings and evdev ioctls.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#endif

#include "ir0/ksft.h"
#include <linux/input-event-codes.h>

#ifndef IR0_SELFTEST_NOLIBC
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* Linux uapi/linux/input.h — not in input-event-codes.h. */
#define BUS_I8042 0x11

/* _IOR('E', 0x02, struct input_id) and _IOC(_IOC_READ, 'E', 0x06, 32). */
#define EVIOCGID 0x80084502u
#define EVIOCGNAME32 0x80204506u
#define IR0_INPUT_IOCTL_INJECT 0x49520001u

#define INJECT_DX 41

struct input_id
{
	uint16_t bustype;
	uint16_t vendor;
	uint16_t product;
	uint16_t version;
};

struct input_event
{
	struct timeval time;
	uint16_t type;
	uint16_t code;
	int32_t value;
};

struct ir0_input_event
{
	uint16_t type;
	uint16_t code;
	int32_t value;
	int64_t timestamp_ms;
};

static void drain_events(int fd)
{
	struct input_event ev;
	int i;

	for (i = 0; i < 8; i++)
	{
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n != (ssize_t)sizeof(ev))
			break;
	}
}

static int name_is_i8042(int fd)
{
	char name[32];
	int rc;

	if (fd < 0)
		return 0;
	memset(name, 0, sizeof(name));
	rc = ioctl(fd, EVIOCGNAME32, name);
	return rc == 10 && memcmp(name, "IR0 i8042", 10) == 0;
}

static int bustype_is_i8042(int fd)
{
	struct input_id id;

	if (fd < 0)
		return 0;
	memset(&id, 0, sizeof(id));
	(void)ioctl(fd, EVIOCGID, &id);
	return id.bustype == BUS_I8042;
}

static int empty_read_is_eagain(int fd)
{
	struct input_event ev;
	ssize_t n;

	if (fd < 0)
		return 0;
	errno = 0;
	n = read(fd, &ev, sizeof(ev));
	return n < 0 && errno == EAGAIN;
}

static int second_reader_sees_event(int first, int second)
{
	struct ir0_input_event inj;
	struct input_event ev;
	ssize_t n;

	if (first < 0 || second < 0)
		return 0;
	memset(&inj, 0, sizeof(inj));
	inj.type = EV_REL;
	inj.code = REL_X;
	inj.value = INJECT_DX;
	if (ioctl(first, IR0_INPUT_IOCTL_INJECT, &inj) != 0)
		return 0;
	/* Consume this open's copy so a shared queue would steal it. */
	(void)read(first, &ev, sizeof(ev));
	memset(&ev, 0, sizeof(ev));
	n = read(second, &ev, sizeof(ev));
	return n == (ssize_t)sizeof(ev) && ev.type == EV_REL &&
	       ev.code == REL_X && ev.value == INJECT_DX;
}

static int mouse_ioctl_is_enotty(void)
{
	char name[32];
	int fd;
	int rc;

	fd = open("/dev/mouse", O_RDONLY | O_NONBLOCK);
	if (fd < 0)
		return 0;
	errno = 0;
	rc = ioctl(fd, EVIOCGNAME32, name);
	close(fd);
	return rc < 0 && errno == ENOTTY;
}

int main(void)
{
	int first;
	int second;

	ksft_set_plan(5);

	first = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
	second = open("/dev/input/event0", O_RDONLY | O_NONBLOCK);
	drain_events(first);
	drain_events(second);

	ksft_test_result(name_is_i8042(first), "evdev_name");
	ksft_test_result(bustype_is_i8042(first), "evdev_bustype");
	ksft_test_result(empty_read_is_eagain(first), "empty_read_eagain");
	ksft_test_result(second_reader_sees_event(first, second),
			 "second_reader_event");
	ksft_test_result(mouse_ioctl_is_enotty(), "non_event_enotty");

	if (first >= 0)
		close(first);
	if (second >= 0)
		close(second);
	return ksft_exit();
}
