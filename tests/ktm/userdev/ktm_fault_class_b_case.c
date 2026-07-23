/**
 * IR0 userspace — KTM Class B fault injection
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_fault_class_b_case.c
 * Description: Arm sched.class_b_arm_window ONCE; fork so switch_to sees *next*.
 *
 * Product kernel (IR0_CLASS_B_REPAIR=1): CLASS_B_FAULT_INJECT +
 * KERNEL_CS_USER_RIP_REPAIR, then KTM_CLASS_B_MITIGATED_OK / KTM_CLASS_B_OK.
 *
 * Repro kernel (IR0_CLASS_B_REPAIR=0): KERNEL_RET_BAD_RIP (runner --done).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

int main(void)
{
	int kfd;
	int fails = 0;
	int status = 0;
	pid_t pid;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_CLASS_B_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_CLASS_B_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	if (!(caps.caps & KTM_CAP_FAULT))
	{
		say("KTM_CLASS_B_SKIP no_cap_fault\n");
		ktm_close(kfd);
		return 0;
	}

	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "fault_class_b") != 0)
	{
		say("KTM_CLASS_B_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}

	(void)ktm_checkpoint(kfd, "arm_class_b");
	if (ktm_config_fault(kfd, "sched.class_b_arm_window", KTM_FAULT_MODE_ONCE,
			     0, 0) != 0)
	{
		(void)ktm_assert_true(kfd, "config_fault", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "config_fault", 1);

	/*
	 * Fork forces switch_to(*next*=child|parent). Fault injects Class B on
	 * that next task once.
	 */
	pid = fork();
	if (pid < 0)
	{
		(void)ktm_assert_true(kfd, "fork", 0);
		fails++;
		goto done;
	}
	if (pid == 0)
	{
		/* Child may be the injected *next*; exit cleanly if repaired. */
		_exit(0);
	}

	if (waitpid(pid, &status, 0) != pid)
	{
		(void)ktm_assert_true(kfd, "waitpid", 0);
		fails++;
		goto done;
	}
	(void)ktm_assert_true(kfd, "waitpid", 1);

	say("KTM_CLASS_B_MITIGATED_OK\n");
	(void)ktm_assert_true(kfd, "survived_inject", 1);

done:
	if (ktm_case_end(kfd, "fault_class_b", fails == 0 ? 0 : 1) != 0)
		fails++;
	ktm_close(kfd);
	if (fails)
	{
		say("KTM_CLASS_B_FAIL\n");
		return 1;
	}
	say("KTM_CLASS_B_OK\n");
	return 0;
}
