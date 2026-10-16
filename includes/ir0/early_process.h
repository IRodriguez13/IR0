/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

/*
 * Staged EL0 is PID 1: session leader and process-group leader.
 * Linux setsid(2) fails with EPERM when the caller is already a
 * process-group leader (man7.org/linux/man-pages/man2/setsid.2.html).
 * getpgid(2)/getsid(2) return ESRCH when the pid does not exist.
 */
#define IR0_EARLY_EPERM 1
#define IR0_EARLY_ESRCH 3

static inline int64_t ir0_early_process_getpgid(uint64_t pid)
{
	if (pid == 0 || pid == 1)
		return 1;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_getsid(uint64_t pid)
{
	return ir0_early_process_getpgid(pid);
}

static inline int64_t ir0_early_process_setsid(void)
{
	return -IR0_EARLY_EPERM;
}

static inline int64_t ir0_early_process_setpgid(uint64_t pid, uint64_t pgid)
{
	if (pid != 0 && pid != 1)
		return -IR0_EARLY_ESRCH;
	(void)pgid;
	return -IR0_EARLY_EPERM;
}

extern const struct syscall_context_provider early_process_provider;
int early_process_smoke_ok(void);
