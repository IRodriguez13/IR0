/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: waitid.c
 * Description: waitid(P_PID, WEXITED) reaps an exited child into siginfo.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#include "../../../arch/arm64/include/uapi/asm/unistd.h"
#include "../../../includes/uapi/linux/wait.h"
#else
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
/*
 * musl declares waitid once a feature macro is set. features.h sets
 * those by default; a strict build without them uses the raw syscall.
 */
#if !defined(_POSIX_SOURCE) && !defined(_POSIX_C_SOURCE) \
 && !defined(_XOPEN_SOURCE) && !defined(_GNU_SOURCE) \
 && !defined(_BSD_SOURCE)
#include <sys/syscall.h>
#define WAITID_VIA_SYSCALL 1
#endif
#endif

#include "ir0/ksft.h"

#ifdef IR0_SELFTEST_NOLIBC
/*
 * Linux siginfo_t is 128 bytes. The kernel copies the whole object:
 * si_pid at offset 16, si_status at offset 24. A short buffer overwrites
 * the caller's stack.
 */
struct waitid_info
{
	int si_signo;
	int si_errno;
	int si_code;
	int si_pad;
	int si_pid;
	int si_uid;
	int si_status;
	char si_rest[128 - 28];
};

_Static_assert(sizeof(struct waitid_info) == 128, "waitid siginfo");
#endif

static int reap_exited(int child, void *info)
{
#ifdef IR0_SELFTEST_NOLIBC
	return (int)syscall(__NR_waitid, P_PID, child, (long)info, WEXITED);
#elif defined(WAITID_VIA_SYSCALL)
	return (int)syscall(__NR_waitid, P_PID, child, (long)info, WEXITED, 0);
#else
	return waitid(P_PID, (id_t)child, info, WEXITED);
#endif
}

int main(void)
{
#ifdef IR0_SELFTEST_NOLIBC
	struct waitid_info info;
#else
	siginfo_t info;
#endif
	int pid;
	int pass;
	int rc;

	ksft_set_plan(1);
	pass = 0;
	info.si_pid = 0;
	info.si_status = 0;
	pid = fork();
	if (pid == 0)
		_exit(7);
	if (pid > 0)
	{
		rc = reap_exited(pid, &info);
		pass = rc == 0 && info.si_pid == pid && info.si_status == 7;
	}
	ksft_test_result(pass, "waitid");
	return ksft_exit();
}
