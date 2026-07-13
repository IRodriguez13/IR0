/**
 * IR0 userspace — KTM init-exit drain case (FASE44 PID1 _exit)
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_init_exit_drain_case.c
 * Description: Spawn/reap children then _exit as PID1 (userdev init).
 *              Optional virtio-9p host share report before exit.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
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
	payload = ok ? "KTM_INIT_EXIT_DRAIN_OK\n" : "KTM_INIT_EXIT_DRAIN_FAIL\n";
	fd = open("/mnt/host/ktm_init_exit_drain.txt", O_WRONLY | O_CREAT | O_TRUNC, 0644);
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
	int fd;
	int fails = 0;
	int started = 0, fork_fail = 0, wait_fail = 0;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_INIT_EXIT_DRAIN_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_INIT_EXIT_DRAIN_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "init_exit_drain") != 0)
	{
		say("KTM_INIT_EXIT_DRAIN_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	(void)ktm_checkpoint(fd, "init_exit_begin");
	if (spawn_and_reap(SPAWN_N, &started, &fork_fail, &wait_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "init_exit_started", (uint64_t)SPAWN_N,
			      (uint64_t)started) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "init_exit_fork_errs", 0,
			      (uint64_t)fork_fail) != 0)
		fails++;
	if (ktm_assert_eq_u64(fd, "init_exit_wait_errs", 0,
			      (uint64_t)wait_fail) != 0)
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else if (ktm_assert_true(fd, "no_zombie_growth",
				 after.zombies <= before.zombies) != 0)
		fails++;

	(void)ktm_case_end(fd, "init_exit_drain", fails == 0 ? 0 : 1);
	ktm_close(fd);

	try_hostshare_report(fails == 0);

	if (fails == 0)
	{
		say("FASE44_INIT_EXIT_DRAIN\n");
		say("KTM_INIT_EXIT_DRAIN_OK\n");
		_exit(0);
	}
	say("KTM_INIT_EXIT_DRAIN_FAIL\n");
	_exit(1);
}
