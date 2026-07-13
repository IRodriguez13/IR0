/**
 * IR0 userspace — KTM input deterministic case (FASE54C analogue)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_input_det_case.c
 * Description: ioctl inject KEY_W press/release on /dev/events0 + readback.
 *              Optional virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

#include "libktm_user.h"

#define EV_KEY 0x01
#define KEY_W 17
#define IR0_INPUT_IOCTL_INJECT 0x49520001u

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

static int64_t now_ms(void)
{
	struct timeval tv;

	if (gettimeofday(&tv, NULL) != 0)
		return 0;
	return (int64_t)tv.tv_sec * 1000LL + (int64_t)tv.tv_usec / 1000LL;
}

static int read_one_key_event(int fd, int expected_value, int timeout_ms)
{
	int64_t deadline = now_ms() + timeout_ms;

	for (;;)
	{
		struct input_event ev;
		ssize_t n = read(fd, &ev, sizeof(ev));

		if (n == (ssize_t)sizeof(ev))
		{
			if (ev.type == EV_KEY && ev.code == KEY_W &&
			    ev.value == expected_value)
				return 0;
		}
		if (now_ms() > deadline)
			return -1;
		usleep(5000);
	}
}

static int run_inject_readback(void)
{
	struct stat st;
	int fd_in;
	struct ir0_input_event inject_ev;

	if (stat("/dev/events0", &st) != 0)
		return -1;
	fd_in = open("/dev/events0", O_RDONLY | O_NONBLOCK);
	if (fd_in < 0)
		return -1;

	memset(&inject_ev, 0, sizeof(inject_ev));
	inject_ev.type = EV_KEY;
	inject_ev.code = KEY_W;
	inject_ev.value = 1;
	if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) != 0)
	{
		close(fd_in);
		return -1;
	}
	inject_ev.value = 0;
	if (ioctl(fd_in, IR0_INPUT_IOCTL_INJECT, &inject_ev) != 0)
	{
		close(fd_in);
		return -1;
	}
	if (read_one_key_event(fd_in, 1, 2000) != 0)
	{
		close(fd_in);
		return -1;
	}
	if (read_one_key_event(fd_in, 0, 2000) != 0)
	{
		close(fd_in);
		return -1;
	}
	close(fd_in);
	return 0;
}

static void try_hostshare_report(int ok)
{
	const char *payload;
	int fd;
	ssize_t n;

	(void)mkdir("/mnt", 0755);
	(void)mkdir("/mnt/host", 0755);
	if (mount("ir0share", "/mnt/host", "9p", 0, NULL) != 0)
	{
		say("KTM_HOSTSHARE_SKIP\n");
		return;
	}
	say("KTM_HOSTSHARE_MOUNT_OK\n");
	payload = ok ? "KTM_USERDEV_INPUT_DET_OK\n" : "KTM_USERDEV_INPUT_DET_FAIL\n";
	fd = open("/mnt/host/ktm_input_det.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
	{
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
		return;
	}
	n = write(fd, payload, strlen(payload));
	(void)close(fd);
	if (n == (ssize_t)strlen(payload))
		say("KTM_HOSTSHARE_REPORT_OK\n");
	else
		say("KTM_HOSTSHARE_WRITE_SKIP\n");
}

int main(void)
{
	int kfd;
	int fails = 0;
	int ok;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_USERDEV_INPUT_DET_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_INPUT_DET_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "input_det") != 0)
	{
		say("KTM_USERDEV_INPUT_DET_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "inject_begin");
	ok = (run_inject_readback() == 0);
	if (!ok)
		fails++;
	if (ktm_assert_true(kfd, "inject_readback", ok) != 0)
		fails++;

	(void)ktm_case_end(kfd, "input_det", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_INPUT_DET_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("KTM_USERDEV_INPUT_DET_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
