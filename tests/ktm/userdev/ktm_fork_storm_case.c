/**
 * IR0 userspace — KTM depth case (FASE42/44 storm analogue)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fork_storm_case.c
 * Description: Real fork+wait storm with /dev/ktm asserts. Optional virtio-9p
 *              host share: mount ir0share → /mnt/host and write result file
 *              (visible on host when QEMU -virtfs is present).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

#define STORM_N 64
#define DRAIN_N 32

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int fork_wait_batch(int n, int *started, int *fork_fail, int *wait_fail)
{
	int i;

	*started = 0;
	*fork_fail = 0;
	*wait_fail = 0;
	for (i = 0; i < n; i++)
	{
		pid_t pid = fork();

		if (pid == 0)
			_exit(0);
		if (pid < 0)
		{
			(*fork_fail)++;
			continue;
		}
		(*started)++;
		if (waitpid(pid, NULL, 0) < 0)
			(*wait_fail)++;
	}
	return (*fork_fail == 0 && *wait_fail == 0 && *started == n) ? 0 : -1;
}

/* Best-effort: if virtio-9p is present, publish result on host share. */
static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "KTM_USERDEV_FORK_STORM_OK\n" : "KTM_USERDEV_FORK_STORM_FAIL\n";
	(void)ktm_hostshare_report("ktm_fork_storm.txt", payload);
}


int main(void)
{
	int fd;
	int fails = 0;
	int started = 0, fork_fail = 0, wait_fail = 0;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FORK_STORM_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_FORK_STORM_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "fork_exit_storm") != 0)
	{
		say("KTM_USERDEV_FORK_STORM_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "storm_begin");
	if (fork_wait_batch(STORM_N, &started, &fork_fail, &wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "storm_started", (uint64_t)STORM_N, (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "storm_wait_errs", 0, (uint64_t)wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "storm_fork_errs", 0, (uint64_t)fork_fail) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "drain_begin");
	if (fork_wait_batch(DRAIN_N, &started, &fork_fail, &wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "drain_started", (uint64_t)DRAIN_N, (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "drain_wait_errs", 0, (uint64_t)wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "drain_fork_errs", 0, (uint64_t)fork_fail) != 0)
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else if (ktm_assert_true(fd, "no_zombie_growth",
				 after.zombies <= before.zombies) != 0)
		fails++;

	(void)ktm_case_end(fd, "fork_exit_storm", fails == 0 ? 0 : 1);
	ktm_close(fd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_FORK_STORM_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FORK_STORM_FAIL\n");
	return 1;
}
