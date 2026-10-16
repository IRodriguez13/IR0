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
#define IR0_EARLY_EINVAL 22

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

/*
 * Linux kill(2): EINVAL if sig is outside 0..NSIG; ESRCH if the pid
 * does not exist. pid 0 is the caller's process group (PID 1 here).
 * pid -1 means "every process except init"; staged EL0 has no other
 * tasks, so that is ESRCH.
 */
static inline int64_t ir0_early_process_kill(int64_t pid, uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (pid == 0 || pid == 1)
		return 0;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_tkill(int64_t tid, uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (tid == 1)
		return 0;
	return -IR0_EARLY_ESRCH;
}

static inline int64_t ir0_early_process_tgkill(int64_t tgid, int64_t tid,
					       uint64_t sig)
{
	if (sig > 64)
		return -IR0_EARLY_EINVAL;
	if (tgid != 0 && tgid != 1)
		return -IR0_EARLY_ESRCH;
	return ir0_early_process_tkill(tid, sig);
}

extern const struct syscall_context_provider early_process_provider;
int early_process_smoke_ok(void);
