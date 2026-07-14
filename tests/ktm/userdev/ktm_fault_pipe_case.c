/**
 * IR0 userspace — KTM fault injection pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fault_pipe_case.c
 * Description: Arm pipe.create ONCE via /dev/ktm; expect fail then recovery.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(void)
{
	int kfd;
	int fails = 0;
	int fds[2];
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_FAULT_PIPE_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_FAULT_PIPE_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	if (!(caps.caps & KTM_CAP_FAULT))
	{
		say("KTM_FAULT_PIPE_SKIP no_cap_fault\n");
		ktm_close(kfd);
		return 0;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "fault_pipe") != 0)
	{
		say("KTM_FAULT_PIPE_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	if (ktm_snapshot_request(kfd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(kfd, "arm_pipe_create");
	if (ktm_config_fault(kfd, "pipe.create", KTM_FAULT_MODE_ONCE, 0, 0) != 0)
	{
		(void)ktm_assert_true(kfd, "config_fault", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "config_fault", 1);

	/* -Iincludes shadows musl <errno.h>; assert fail/recover without errno. */
	if (pipe(fds) == 0)
	{
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)ktm_assert_true(kfd, "pipe_fault_hit", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "pipe_fault_hit", 1);
		say("KTM_FAULT_PIPE_ENOMEM_OK\n");
	}

	(void)ktm_checkpoint(kfd, "pipe_recover");
	if (pipe(fds) != 0)
	{
		(void)ktm_assert_true(kfd, "pipe_recover", 0);
		fails++;
	}
	else
	{
		(void)close(fds[0]);
		(void)close(fds[1]);
		(void)ktm_assert_true(kfd, "pipe_recover", 1);
	}

	fails += ktm_assert_no_leaks(kfd, &before);

done:
	(void)ktm_case_end(kfd, "fault_pipe", fails == 0 ? 0 : 1);
	ktm_close(kfd);

	if (fails == 0)
	{
		say("KTM_SNAPSHOT_DELTA_OK\n");
		say("KTM_FAULT_PIPE_OK\n");
		say("KTM_USERDEV_OK\n");
		_exit(0);
	}
	say("KTM_FAULT_PIPE_FAIL\n");
	_exit(1);
}
