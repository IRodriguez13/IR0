/**
 * IR0 userspace — KTM hybrid pilot
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fork_wait_case.c
 * Description: PID1 smoke — /dev/ktm case fork/wait + kernel scenario.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "libktm_user.h"

#define KTM_INV_PROCESS (1u << 0)
#define KTM_INV_FRAMES  (1u << 1)

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
	ktm_ioc_snapshot_t before;
	int32_t scen_rc = -1;

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
	if (ktm_case_begin(fd, "fork_wait_signal") != 0)
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
		pause();
		_exit(0);
	}

	(void)ktm_assert_true(fd, "fork_ok", 1);
	(void)ktm_checkpoint(fd, "child_spawned");

	if (kill(pid, SIGTERM) != 0)
	{
		(void)ktm_assert_true(fd, "kill_ok", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(fd, "kill_ok", 1);

	if (waitpid(pid, &status, 0) != pid)
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(fd, "waitpid_ok", 1);
		(void)ktm_assert_true(fd, "wtermsig_sigterm",
				      WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
		if (!(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM))
			fails++;
	}

	if (ktm_run_invariants(fd, KTM_INV_PROCESS | KTM_INV_FRAMES) != 0)
		fails++;

	fails += ktm_assert_no_leaks(fd, &before);

	if (ktm_run_scenario(fd, "process.lifecycle", &scen_rc) != 0 || scen_rc != 0)
	{
		(void)ktm_assert_true(fd, "scenario_process_lifecycle", 0);
		fails++;
	}
	else
		(void)ktm_assert_true(fd, "scenario_process_lifecycle", 1);

done:
	(void)ktm_case_end(fd, "fork_wait_signal", fails == 0 ? 0 : 1);
	ktm_close(fd);

	if (fails == 0)
	{
		say("KTM_USERDEV_OK\n");
		return 0;
	}
	say("KTM_USERDEV_FAIL\n");
	return 1;
}
