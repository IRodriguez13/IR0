/**
 * IR0 userspace — KTM hybrid pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_cow_touch_case.c
 * Description: PID1 — fork + child write shared page (COW touch MVP).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

static volatile char g_cow_page[4096] __attribute__((aligned(4096)));

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(void)
{
	int fd;
	int fails = 0;
	pid_t pid;
	int status = -1;
	ktm_user_caps_t caps;
	ktm_ioc_snapshot_t before, after;

	memset((void *)g_cow_page, 'A', sizeof(g_cow_page));

	fd = ktm_open();
	if (fd < 0)
	{
		say("KTM_USERDEV_FAIL open\n");
		return 1;
	}

	if (ktm_get_caps(fd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_USERDEV_FAIL caps\n");
		ktm_close(fd);
		return 1;
	}

	(void)ktm_reset(fd);
	if (ktm_case_begin(fd, "cow_touch") != 0)
	{
		say("KTM_USERDEV_FAIL case_begin\n");
		ktm_close(fd);
		return 1;
	}

	if (ktm_snapshot_request(fd, &before) != 0)
		fails++;

	pid = fork();
	if (pid < 0)
	{
		(void)ktm_assert_true(fd, "fork_ok", 0);
		fails++;
		goto done;
	}
	if (pid == 0)
	{
		g_cow_page[0] = 'B';
		_exit(g_cow_page[0] == 'B' ? 0 : 2);
	}

	(void)ktm_assert_true(fd, "fork_ok", 1);
	(void)ktm_checkpoint(fd, "child_spawned");

	if (waitpid(pid, &status, 0) != pid)
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 1);
		(void)ktm_assert_true(fd, "child_exit_ok",
				      WIFEXITED(status) && WEXITSTATUS(status) == 0);
		if (!(WIFEXITED(status) && WEXITSTATUS(status) == 0))
			fails++;
	}

	/* Parent page must remain 'A' if COW (or shared-then-copy) worked. */
	(void)ktm_assert_true(fd, "parent_page_intact", g_cow_page[0] == 'A');
	if (g_cow_page[0] != 'A')
		fails++;

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	if (ktm_snapshot_request(fd, &after) != 0)
		fails++;
	else
	{
		(void)ktm_assert_true(fd, "no_zombie_growth",
				      after.zombies <= before.zombies);
		if (after.zombies > before.zombies)
			fails++;
	}

done:
	(void)ktm_case_end(fd, "cow_touch", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_COW_OK\n");
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
