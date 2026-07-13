/**
 * IR0 userspace — KTM reap-drain case (FASE44 init-exit drain analogue)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_reap_drain_case.c
 * Description: Spawn short-lived children and wait4(-1) drain with /dev/ktm.
 *              Does not exit PID1 — see `ktm_init_exit_drain_case.c`. Optional
 *              virtio-9p host share report.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

#define SPAWN_N 8

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static void drain_children(void)
{
	pid_t r;

	while ((r = wait4(-1, NULL, WNOHANG, NULL)) > 0)
		(void)r;
	for (;;)
	{
		r = wait4(-1, NULL, 0, NULL);
		if (r < 0)
			break;
	}
}

static int spawn_and_reap(int n, int *started, int *fork_fail, int *wait_fail)
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
	drain_children();
	return (*fork_fail == 0 && *wait_fail == 0 && *started == n) ? 0 : -1;
}

static void try_hostshare_report(int ok)
{
	const char *payload = ok ? "KTM_USERDEV_REAP_DRAIN_OK\n" : "KTM_USERDEV_REAP_DRAIN_FAIL\n";
	(void)ktm_hostshare_report("ktm_reap_drain.txt", payload);
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
		say("KTM_USERDEV_REAP_DRAIN_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_REAP_DRAIN_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "reap_drain") != 0)
	{
		say("KTM_USERDEV_REAP_DRAIN_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "reap_drain_begin");
	if (spawn_and_reap(SPAWN_N, &started, &fork_fail, &wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "reap_started", (uint64_t)SPAWN_N, (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "reap_fork_errs", 0, (uint64_t)fork_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "reap_wait_errs", 0, (uint64_t)wait_fail) != 0)
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else if (ktm_assert_true(fd, "no_zombie_growth",
				 after.zombies <= before.zombies) != 0)
		fails++;

	(void)ktm_case_end(fd, "reap_drain", fails == 0 ? 0 : 1);
	ktm_close(fd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("KTM_USERDEV_REAP_DRAIN_OK\n");
		for (;;)
			(void)pause();
		return 0;
	}
	say("KTM_USERDEV_REAP_DRAIN_FAIL\n");
	for (;;)
		(void)pause();
	return 1;
}
