/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_process.c
 * Description: public entry points and syscall provider tables for staged
 *              early process; state and helpers live in early_process_state.c /
 *              early_process_helpers.c.
 */

#include "early_process_internal.h"

#include <stdint.h>

static const enum ir0_syscall_id g_identity_syscalls[] = {
	IR0_SYSCALL_GETPID, IR0_SYSCALL_GETTID,
	IR0_SYSCALL_GETUID, IR0_SYSCALL_GETEUID, IR0_SYSCALL_GETGID,
	IR0_SYSCALL_GETEGID, IR0_SYSCALL_GETPPID,
};

const struct syscall_context_provider early_process_identity_provider = {
	.ids = g_identity_syscalls,
	.count = sizeof(g_identity_syscalls) / sizeof(g_identity_syscalls[0]),
	.handler = ep_early_process_syscall,
};

static const enum ir0_syscall_id g_process_syscalls[] = {
	IR0_SYSCALL_SET_TID_ADDRESS,
	IR0_SYSCALL_UNAME,
	IR0_SYSCALL_SET_ROBUST_LIST, IR0_SYSCALL_PRCTL, IR0_SYSCALL_PRLIMIT64,
	IR0_SYSCALL_RSEQ, 	IR0_SYSCALL_GETRANDOM, IR0_SYSCALL_WAIT4,
	IR0_SYSCALL_WAITID, IR0_SYSCALL_CLONE, IR0_SYSCALL_CLONE3,
	IR0_SYSCALL_VFORK,
	IR0_SYSCALL_GETPGID, IR0_SYSCALL_GETSID, IR0_SYSCALL_SETSID,
	IR0_SYSCALL_SETPGID, IR0_SYSCALL_UMASK, IR0_SYSCALL_GETGROUPS,
	IR0_SYSCALL_SETGROUPS, IR0_SYSCALL_RT_SIGPROCMASK,
	IR0_SYSCALL_RT_SIGACTION, IR0_SYSCALL_RT_SIGSUSPEND,
	IR0_SYSCALL_REBOOT, IR0_SYSCALL_KILL, IR0_SYSCALL_TKILL,
	IR0_SYSCALL_TGKILL, IR0_SYSCALL_GETRESUID, IR0_SYSCALL_GETRESGID,
	IR0_SYSCALL_GETRLIMIT, IR0_SYSCALL_GETRUSAGE, IR0_SYSCALL_SYSINFO,
	IR0_SYSCALL_PERSONALITY, IR0_SYSCALL_SCHED_YIELD,
	IR0_SYSCALL_GETCPU, IR0_SYSCALL_SCHED_SETPARAM,
	IR0_SYSCALL_SCHED_SETSCHEDULER, IR0_SYSCALL_SCHED_GETSCHEDULER,
	IR0_SYSCALL_SCHED_GETPARAM, IR0_SYSCALL_SCHED_SETAFFINITY,
	IR0_SYSCALL_SCHED_GETAFFINITY, 	IR0_SYSCALL_PROCESS_VM_READV,
	IR0_SYSCALL_PROCESS_VM_WRITEV, IR0_SYSCALL_PROCESS_MADVISE,
	IR0_SYSCALL_MEMBARRIER,
	IR0_SYSCALL_TIMES, IR0_SYSCALL_SETHOSTNAME, IR0_SYSCALL_SETDOMAINNAME,
	IR0_SYSCALL_CAPGET, IR0_SYSCALL_CAPSET, IR0_SYSCALL_IOPRIO_SET,
	IR0_SYSCALL_IOPRIO_GET, IR0_SYSCALL_SCHED_GET_PRIORITY_MAX,
	IR0_SYSCALL_SCHED_GET_PRIORITY_MIN, IR0_SYSCALL_SCHED_RR_GET_INTERVAL,
	IR0_SYSCALL_SCHED_SETATTR, IR0_SYSCALL_SCHED_GETATTR, IR0_SYSCALL_KCMP,
	IR0_SYSCALL_PIDFD_OPEN, IR0_SYSCALL_PIDFD_SEND_SIGNAL,
	IR0_SYSCALL_PIDFD_GETFD,
	IR0_SYSCALL_SIGALTSTACK, IR0_SYSCALL_FUTEX,
	IR0_SYSCALL_FUTEX_WAITV, IR0_SYSCALL_FUTEX_WAKE,
	IR0_SYSCALL_FUTEX_WAIT, IR0_SYSCALL_FUTEX_REQUEUE,
	IR0_SYSCALL_LSM_GET_SELF_ATTR, IR0_SYSCALL_LSM_SET_SELF_ATTR,
	IR0_SYSCALL_LSM_LIST_MODULES, IR0_SYSCALL_UNSHARE, IR0_SYSCALL_SETNS,
	IR0_SYSCALL_PROCESS_MRELEASE,
	IR0_SYSCALL_SETUID,
	IR0_SYSCALL_SETGID, IR0_SYSCALL_SETFSUID, IR0_SYSCALL_SETFSGID,
	IR0_SYSCALL_SETREUID, IR0_SYSCALL_SETREGID,
	IR0_SYSCALL_SETRESUID, IR0_SYSCALL_SETRESGID, IR0_SYSCALL_GETPRIORITY,
	IR0_SYSCALL_SETPRIORITY, IR0_SYSCALL_GET_ROBUST_LIST,
	IR0_SYSCALL_SYSLOG, IR0_SYSCALL_EXECVEAT, IR0_SYSCALL_EXECVE,
	IR0_SYSCALL_EXIT, IR0_SYSCALL_EXIT_GROUP,
};

const struct syscall_context_provider early_process_provider = {
	.ids = g_process_syscalls,
	.count = sizeof(g_process_syscalls) / sizeof(g_process_syscalls[0]),
	.handler = ep_early_process_syscall,
};

int early_process_is_root(void)
{
	return ep_g_euid == 0;
}

void early_process_note_exec(void)
{
	if (ep_g_current_pid == ep_g_child_pid && ep_g_vfork)
	{
		ep_g_vfork = 0;
		ep_g_vfork_exec = 1;
		ep_g_resume_parent = 1;
	}
}

int early_process_request_yield(void)
{
	if (ep_g_current_pid == ep_g_child_pid)
	{
		if (ep_g_vfork || ep_g_child_state == EP_CHILD_ZOMBIE)
			return 0;
		ep_g_yield_requested = 1;
		return 1;
	}
	if (ep_g_child_state == EP_CHILD_RUNNING || ep_g_child_state == EP_CHILD_PARKED)
	{
		ep_g_yield_requested = 1;
		return 1;
	}
	return 0;
}

int64_t early_process_current_pid(void)
{
	return ep_g_current_pid;
}

void early_process_current_ucred(int32_t *pid, int32_t *uid, int32_t *gid)
{
	if (pid)
		*pid = (int32_t)ep_g_current_pid;
	if (uid)
		*uid = (int32_t)ep_g_uid;
	if (gid)
		*gid = (int32_t)ep_g_gid;
}

void early_process_bind_common_exit(int (*fn)(int code))
{
	ep_g_common_task_exit = fn;
}

void early_process_bind_schedule_exit(void (*fn)(int code))
{
	ep_g_schedule_exit = fn;
}

