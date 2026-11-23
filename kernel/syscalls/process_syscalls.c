/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process_syscalls.c
 * Description: process/credential/signal syscall helpers
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_syscalls.h"
#include <string.h>
#include <ir0/acpi_pm.h>
#include <asm/pgtable.h>
#include <ir0/tls.h>
#include <ir0/clock.h>
#include <ir0/clone.h>
#include <ir0/copy_user.h>
#include <ir0/cred.h>
#include <ir0/kexec.h>
#include <ir0/ktm/klog.h>
#include <ir0/oops.h>
#include <ir0/power_manag.h>
#include <ir0/sched.h>
#include <ir0/wait_status.h>

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* Linux uapi si_code for SIGCHLD (asm-generic/siginfo.h). */
#define CLD_EXITED 1
#define CLD_KILLED 2
#define CLD_DUMPED 3

_Static_assert(offsetof(siginfo_t, _sifields) == 16,
	       "waitid si_pid is the siginfo union");

/* prctl(2) options (Linux include/uapi/linux/prctl.h) */
#define PR_SET_PDEATHSIG 1
#define PR_GET_PDEATHSIG 2
#define PR_GET_DUMPABLE 3
#define PR_SET_DUMPABLE 4
#define PR_GET_KEEPCAPS 7
#define PR_SET_KEEPCAPS 8
#define PR_SET_NAME 15
#define PR_GET_NAME 16
#define PR_SET_NO_NEW_PRIVS 38
#define PR_GET_NO_NEW_PRIVS 39

/* Credentials, supplementary groups, and Linux setreuid/setresuid ABI. */

int64_t sys_getgroups(int size, gid_t *list)
{
	uint8_t n;
	int i;

	if (!current_process)
		return -ESRCH;

	n = current_process->ngroups;

	if (size == 0)
		return (int64_t)n;

	if (!list || size < 0)
		return -EINVAL;
	if (size < (int)n)
		return -EINVAL;

	for (i = 0; i < (int)n; i++)
	{
		gid_t g = current_process->groups[i];
		if (copy_to_user(&list[i], &g, sizeof(g)) != 0)
			return -EFAULT;
	}
	return (int64_t)n;
}

int64_t sys_setgroups(size_t size, const gid_t *list)
{
	gid_t groups[IR0_NGROUPS_MAX];
	size_t i;

	if (!current_process)
		return -ESRCH;
	if (current_process->euid != ROOT_UID)
		return -EPERM;
	if (size > IR0_NGROUPS_MAX)
		return -EINVAL;
	/* Copy the whole list first: a fault part-way must change nothing. */
	if (size > 0 && copy_from_user(groups, list, size * sizeof(gid_t)) != 0)
		return -EFAULT;
	/* Linux groups_from_user: (gid_t)-1 is not a valid group. */
	for (i = 0; i < size; i++)
	{
		if (groups[i] == (gid_t)-1)
			return -EINVAL;
	}

	for (i = 0; i < size; i++)
		current_process->groups[i] = groups[i];
	current_process->ngroups = (uint8_t)size;
	return 0;
}

static int cred_copy_out_u32(void *uptr, uint32_t value)
{
	if (!uptr)
		return 0;
	if (copy_to_user(uptr, &value, sizeof(value)) != 0)
		return -EFAULT;
	return 0;
}

static void process_cred_load_uid(const process_t *p, ir0_cred_id_triplet_t *ids)
{
	ids->real = p->uid;
	ids->effective = p->euid;
	ids->saved = p->suid;
}

static void process_cred_store_uid(process_t *p, const ir0_cred_id_triplet_t *ids)
{
	p->uid = ids->real;
	p->euid = ids->effective;
	p->suid = ids->saved;
}

static void process_cred_load_gid(const process_t *p, ir0_cred_id_triplet_t *ids)
{
	ids->real = p->gid;
	ids->effective = p->egid;
	ids->saved = p->sgid;
}

static void process_cred_store_gid(process_t *p, const ir0_cred_id_triplet_t *ids)
{
	p->gid = ids->real;
	p->egid = ids->effective;
	p->sgid = ids->saved;
}

int64_t sys_setreuid(uid_t ruid, uid_t euid)
{
	ir0_cred_id_triplet_t ids;
	int ret;

	if (!current_process)
		return -ESRCH;

	process_cred_load_uid(current_process, &ids);
	ret = ir0_cred_setreid(&ids, (uint32_t)ruid, (uint32_t)euid,
			       current_process->euid == ROOT_UID);
	if (ret < 0)
		return ret;
	process_cred_store_uid(current_process, &ids);
	return 0;
}

int64_t sys_setregid(gid_t rgid, gid_t egid)
{
	ir0_cred_id_triplet_t ids;
	int ret;

	if (!current_process)
		return -ESRCH;

	process_cred_load_gid(current_process, &ids);
	ret = ir0_cred_setreid(&ids, (uint32_t)rgid, (uint32_t)egid,
			       current_process->euid == ROOT_UID);
	if (ret < 0)
		return ret;
	process_cred_store_gid(current_process, &ids);
	return 0;
}

int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	ir0_cred_id_triplet_t ids;
	int ret;

	if (!current_process)
		return -ESRCH;

	process_cred_load_uid(current_process, &ids);
	ret = ir0_cred_setresid(&ids, (uint32_t)ruid, (uint32_t)euid,
				(uint32_t)suid,
				current_process->euid == ROOT_UID);
	if (ret < 0)
		return ret;
	process_cred_store_uid(current_process, &ids);
	return 0;
}

int64_t sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	uid_t u;
	uid_t eu;
	uid_t su;
	int ret;

	if (!current_process)
		return -ESRCH;

	u = (uid_t)current_process->uid;
	eu = (uid_t)current_process->euid;
	su = (uid_t)current_process->suid;

	ret = cred_copy_out_u32(ruid, u);
	if (ret < 0)
		return ret;
	ret = cred_copy_out_u32(euid, eu);
	if (ret < 0)
		return ret;
	return cred_copy_out_u32(suid, su);
}

int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	ir0_cred_id_triplet_t ids;
	int ret;

	if (!current_process)
		return -ESRCH;

	process_cred_load_gid(current_process, &ids);
	ret = ir0_cred_setresid(&ids, (uint32_t)rgid, (uint32_t)egid,
				(uint32_t)sgid,
				current_process->euid == ROOT_UID);
	if (ret < 0)
		return ret;
	process_cred_store_gid(current_process, &ids);
	return 0;
}

int64_t sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	gid_t g;
	gid_t eg;
	gid_t sg;
	int ret;

	if (!current_process)
		return -ESRCH;

	g = (gid_t)current_process->gid;
	eg = (gid_t)current_process->egid;
	sg = (gid_t)current_process->sgid;

	ret = cred_copy_out_u32(rgid, g);
	if (ret < 0)
		return ret;
	ret = cred_copy_out_u32(egid, eg);
	if (ret < 0)
		return ret;
	return cred_copy_out_u32(sgid, sg);
}

/* Session, process group, personality, and nice. */

int64_t sys_setsid(void)
{
	pid_t pid;

	if (!current_process)
		return -ESRCH;

	pid = current_process->task.pid;
	/* Already a session leader. */
	if (current_process->sid == pid)
		return -EPERM;

	current_process->sid = pid;
	current_process->pgid = pid;
	return (int64_t)pid;
}

int64_t sys_getsid(pid_t pid)
{
	process_t *target;

	if (!current_process)
		return -ESRCH;

	target = (pid == 0) ? current_process : process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	return (int64_t)target->sid;
}

/*
 * personality(2) execution domains (Linux uapi/linux/personality.h). Only the
 * two domains that change observable behavior here are accepted: PER_LINUX32
 * is what linux32(1) sets so uname(2) reports a 32-bit machine.
 */
#define PER_LINUX 0x0000
#define PER_LINUX32 0x0008
#define PERSONALITY_QUERY 0xffffffffu

int64_t sys_personality(unsigned long persona)
{
	uint32_t old;

	if (!current_process)
		return -ESRCH;

	old = current_process->personality;
	if ((uint32_t)persona == PERSONALITY_QUERY)
		return (int64_t)old;

	if (persona != PER_LINUX && persona != PER_LINUX32)
		return -EINVAL;

	current_process->personality = (uint32_t)persona;
	return (int64_t)old;
}

/* which values for setpriority(2)/getpriority(2) (Linux uapi/linux/resource.h). */
#define PRIO_PROCESS 0
#define PRIO_PGRP 1
#define PRIO_USER 2

/*
 * Project a Linux nice value (-20..19) onto IR0's 8 priority bands, keeping
 * nice 0 on the default band and lower nice on higher bands. Backends other
 * than CONFIG_SCHEDULER "prio" ignore sched_prio, so there the nice value
 * is recorded and reported but does not change scheduling.
 */
static int prio_band_from_nice(int nice_val)
{
	int band = IR0_SCHED_PRIO_DEFAULT - (nice_val / 5);

	if (band < 0)
		return 0;
	if (band > IR0_SCHED_PRIO_MAX)
		return IR0_SCHED_PRIO_MAX;
	return band;
}

/*
 * setpriority(2). Only PRIO_PROCESS is implemented; PRIO_PGRP/PRIO_USER
 * report -EOPNOTSUPP rather than pretending to have applied the change.
 */
int64_t sys_setpriority(int which, int who, int prio)
{
	process_t *target;
	int band;

	if (!current_process)
		return -ESRCH;
	if (which != PRIO_PROCESS)
	{
		if (which == PRIO_PGRP || which == PRIO_USER)
			return -EOPNOTSUPP;
		return -EINVAL;
	}

	target = (who == 0) ? current_process
			    : process_find_by_pid((pid_t)who);
	if (!target)
		return -ESRCH;

	if (prio < -20)
		prio = -20;
	if (prio > 19)
		prio = 19;

	/* Raising priority (lowering nice) is privileged, as in Linux. */
	if (prio < target->sched_nice && current_process->euid != ROOT_UID)
		return -EACCES;

	target->sched_nice = (int8_t)prio;
	band = prio_band_from_nice(prio);
	if (band != target->sched_prio)
	{
		/* Re-queue so the band change takes effect on the next pick. */
		sched_remove(target);
		target->sched_prio = band;
		sched_add(target);
	}
	return 0;
}

/*
 * getpriority(2). The raw syscall returns the nice value biased by 20 so a
 * legitimate negative nice is not mistaken for an error code; the libc
 * undoes the bias (Linux kernel/sys.c).
 */
/* sched_yield(2): let every other runnable task run first; never fails. */
int64_t sys_sched_yield(void)
{
	if (!current_process)
		return -ESRCH;
	schedule();
	return 0;
}

int64_t sys_getpriority(int which, int who)
{
	process_t *target;

	if (!current_process)
		return -ESRCH;
	if (which != PRIO_PROCESS)
	{
		if (which == PRIO_PGRP || which == PRIO_USER)
			return -EOPNOTSUPP;
		return -EINVAL;
	}

	target = (who == 0) ? current_process
			    : process_find_by_pid((pid_t)who);
	if (!target)
		return -ESRCH;

	return (int64_t)(20 - (int)target->sched_nice);
}

int64_t sys_getpgid(pid_t pid)
{
	process_t *target;

	if (!current_process)
		return -ESRCH;

	target = (pid == 0) ? current_process : process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	return (int64_t)target->pgid;
}

int64_t sys_setpgid(pid_t pid, pid_t pgid)
{
	process_t *target;
	pid_t self;

	if (!current_process)
		return -ESRCH;

	self = current_process->task.pid;
	if (pid == 0)
		pid = self;
	if (pgid < 0)
		return -EINVAL;

	target = process_find_by_pid(pid);
	if (!target)
		return -ESRCH;

	/* Only self or a direct child in the same session. */
	if (target != current_process)
	{
		if (target->ppid != self)
			return -ESRCH;
		if (target->sid != current_process->sid)
			return -EPERM;
	}

	if (pgid == 0)
		pgid = target->task.pid;

	/* Session leaders cannot leave their process group. */
	if (target->sid == target->task.pid && pgid != target->task.pid)
		return -EPERM;

	/* pgid must be an existing group in the same session, or create own. */
	if (pgid != target->task.pid)
	{
		process_t *scan;
		int found = 0;

		for (scan = process_list; scan; scan = scan->next)
		{
			if (scan->pgid == pgid && scan->sid == target->sid)
			{
				found = 1;
				break;
			}
		}
		if (!found)
			return -EPERM;
	}

	target->pgid = pgid;
	return 0;
}

/* Exit, reboot, and identity getters/setters. */

int64_t sys_exit(int exit_code)
{
  if (!current_process)
    return -ESRCH;

  /* Use process_exit() for complete cleanup:
   * - Reparents children to init
   * - Reaps zombie children
   * - Sends SIGCHLD to parent
   * - Removes from scheduler
   * - Switches to another process
   * This function never returns */
  process_exit(exit_code);

  /* Should never reach here - process_exit() switches context */
  panicex("sys_exit: process_exit() returned (should not happen)", RUNNING_OUT_PROCESS, __FILE__, __LINE__, __func__);
  return 0;
}

int64_t sys_exit_group(int exit_code)
{
  return sys_exit(exit_code);
}

int64_t sys_reboot(int magic1, int magic2, unsigned int cmd, void *arg)
{
	int ret;

	(void)arg;

	if (!ir0_cred_is_root())
		return -EPERM;

	if (magic1 != (int)LINUX_REBOOT_MAGIC1 ||
	    (magic2 != (int)LINUX_REBOOT_MAGIC2 &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2A &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2B &&
	     magic2 != (int)LINUX_REBOOT_MAGIC2C))
		return -EINVAL;

	switch (cmd)
	{
	case LINUX_REBOOT_CMD_CAD_ON:
		klog_smoke("REBOOT_CAD_ON");
		return 0;
	case LINUX_REBOOT_CMD_CAD_OFF:
		klog_smoke("REBOOT_CAD_OFF");
		return 0;
	case LINUX_REBOOT_CMD_RESTART:
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_RESTART2:
		klog_smoke("REBOOT_RESTART2");
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_HALT:
		kernel_system_shutdown(IR0_SYSTEM_HALT);
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_system_shutdown(IR0_SYSTEM_POWEROFF);
		break;
	case LINUX_REBOOT_CMD_KEXEC:
		if (ir0_kexec_image_loaded())
		{
			ir0_kexec_execute();
			/* Magic payload returns here; soft reboot. */
			kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		}
		klog_smoke("REBOOT_KEXEC_STUB");
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_SW_SUSPEND:
		/*
		 * Linux fails this op with -EINVAL when hibernation support is
		 * absent; here the ACPI layer decides, so surface its result
		 * instead of claiming an unconditional suspend.
		 */
		ret = ir0_acpi_pm_try_suspend();
		if (ret < 0)
			return ret;
		return 0;
	default:
		return -EINVAL;
	}

	/* noreturn paths above */
	return 0;
}

int64_t sys_getpid(void)
{
  if (!current_process)
    return -ESRCH;
  return process_pid(current_process);
}

int64_t sys_gettid(void)
{
  pid_t tid;

  if (!current_process)
    return -ESRCH;

  tid = process_pid(current_process);

  return (int64_t)tid;
}

int64_t sys_getppid(void)
{
  if (!current_process)
    return -ESRCH;
  /* Parent pid recorded at fork/exec */
  return (int64_t)current_process->ppid;
}

int64_t sys_getuid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->uid;
}

int64_t sys_geteuid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->euid;
}

int64_t sys_getgid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->gid;
}

int64_t sys_getegid(void)
{
  if (!current_process)
    return -ESRCH;
  return (int64_t)current_process->egid;
}

int64_t sys_setuid(uid_t uid)
{
  if (!current_process)
    return -ESRCH;

  /* Privileged: real, effective and saved UID all become @uid (no way back). */
  if (current_process->euid == ROOT_UID)
  {
    current_process->uid = (uint32_t)uid;
    current_process->euid = (uint32_t)uid;
    current_process->suid = (uint32_t)uid;
    return 0;
  }

  /* Unprivileged: only the effective UID moves, and only to real or saved. */
  if ((uint32_t)uid == current_process->uid || (uint32_t)uid == current_process->suid)
  {
    current_process->euid = (uint32_t)uid;
    return 0;
  }

  return -EPERM;
}

int64_t sys_setgid(gid_t gid)
{
  if (!current_process)
    return -ESRCH;

  if (current_process->euid == ROOT_UID)
  {
    current_process->gid = (uint32_t)gid;
    current_process->egid = (uint32_t)gid;
    current_process->sgid = (uint32_t)gid;
    return 0;
  }

  if ((uint32_t)gid == current_process->gid || (uint32_t)gid == current_process->sgid)
  {
    current_process->egid = (uint32_t)gid;
    return 0;
  }

  return -EPERM;
}

int64_t sys_umask(mode_t mask)
{
  if (!current_process)
    return -ESRCH;

  mode_t old = (mode_t)(current_process->umask & 0777U);
  current_process->umask = (uint32_t)(mask & 0777U);
  return (int64_t)old;
}

/* fork / vfork / clone / wait. */

int64_t sys_fork(void)
{
  int64_t r;
  if (!current_process)
    return -ESRCH;

  r = fork();
  return r;
}

/*
 * vfork(2) — Linux kernel_clone(CLONE_VFORK|CLONE_VM): parent remains blocked
 * until the child execs or _exits, and child writes are visible in the shared
 * mm. See kernel/fork.c wait_for_vfork_done / complete_vfork_done.
 */
int64_t sys_vfork(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)vfork_process(NULL, 0);
}

/*
 * sys_clone — Linux __NR_clone (56).
 * CLONE_VFORK|CLONE_VM: vfork semantics (no CLONE_THREAD).
 * CLONE_THREAD|CLONE_VM: lightweight thread sharing the caller's mm.
 * CLONE_SIGHAND: parent and child share one handler table.
 * Otherwise duplicates the address space like fork().
 * stack is the child user RSP (clone(2) child stack); NULL keeps the
 * parent's stack (classic vfork).
 */
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
                  int *child_tid, unsigned long tls)
{
	if (!current_process)
		return -ESRCH;

	/*
	 * Linux copy_process(): a thread shares the handlers (CLONE_THREAD
	 * needs CLONE_SIGHAND), and shared handlers need a shared address
	 * space (CLONE_SIGHAND needs CLONE_VM). Anything else is EINVAL.
	 */
	if ((flags & CLONE_THREAD) && !(flags & CLONE_SIGHAND))
		return -EINVAL;
	if ((flags & CLONE_SIGHAND) && !(flags & CLONE_VM))
		return -EINVAL;

	if (flags & CLONE_VFORK)
	{
		if ((flags & CLONE_THREAD) || !(flags & CLONE_VM))
			return -EINVAL;
		return (int64_t)vfork_process(stack, flags);
	}

	if (flags & CLONE_THREAD)
		return (int64_t)clone_thread(flags, stack, parent_tid, child_tid, tls);

	(void)parent_tid;
	(void)child_tid;
	(void)tls;
	return fork_with_stack(stack, flags);
}


/* Body of sys_wait4 once pid and options are already the wait4 ABI. */
static int64_t wait4_reap(pid_t pid, int *status, int options)
{
  /* Linux kernel_wait4(): unknown option bits fail before any lookup. */
  if ((unsigned int)options & ~(unsigned int)WAIT4_VALID_OPTIONS)
    return -EINVAL;
  if (current_process->mode == USER_MODE)
    process_wait_options_set(current_process, options);

  if (IR0_DEBUG_WAIT)
  {
    klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] wait_begin parent=%x target=%x options=%x wait_target_pid=%x wait_options=%x", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid), (unsigned)((uint32_t)options), (unsigned)((uint32_t)process_wait_target_pid(current_process)), (unsigned)((uint32_t)process_wait_options(current_process)));
    klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][sys_wait4] entry parent_pid=%x wait_pid=%x status_ptr=%llx options=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned)((uint32_t)pid), (unsigned long long)((uint64_t)(uintptr_t)status), (unsigned long long)((uint64_t)(unsigned int)options));
  }

  {
    int64_t ret = process_wait(pid, status, options);

    if (IR0_DEBUG_WAIT)
    {
      klog_debug_fmt("WAIT", "[WAIT4_WNOHANG_AUDIT] wait_return parent=%x ret=%llx status_write=%s", (unsigned)((uint32_t)current_process->task.pid), (unsigned long long)((uint64_t)ret), ret > 0 ? "yes" : "no");
      klog_debug_fmt("WAIT", "[WAIT_EXIT_AUDIT][sys_wait4] return parent_pid=%x ret=%llx", (unsigned)((uint32_t)current_process->task.pid), (unsigned long long)((uint64_t)ret));
    }
    return ret;
  }
}

int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  if (current_process->mode == USER_MODE)
  {
    /* Read the wait arguments from the syscall frame (pt_regs). */
    pid = (pid_t)process_syscall_arg(current_process, 0);
    options = (int)process_syscall_arg(current_process, 2);
  }

  return wait4_reap(pid, status, options);
}

int64_t sys_waitpid(pid_t pid, int *status, int options)
{
  return sys_wait4(pid, status, options, NULL);
}

/*
 * sys_waitid — Linux waitid(2) for an exited child.
 * WEXITED is required. WNOHANG is the only other accepted bit: WSTOPPED
 * and WCONTINUED have no stopped state here and return EINVAL.
 * The zombie is reaped by wait4_reap (the body of sys_wait4). Success is
 * 0; the child pid and exit code go into the signal-path siginfo.
 * A null infop still reaps. No matching child is ECHILD, or 0 with WNOHANG.
 */
int64_t sys_waitid(int idtype, pid_t id, siginfo_t *infop, int options,
		   void *ru)
{
	pid_t wait_pid;
	int wait_opts;
	int raw;
	int child_pid;
	int child_status;
	int64_t ret;
	siginfo_t info;
	int *slot;

	(void)ru;

	if (!current_process)
		return -ESRCH;

	if (current_process->mode == USER_MODE)
	{
		idtype = (int)process_syscall_arg(current_process, 0);
		id = (pid_t)process_syscall_arg(current_process, 1);
		infop = (siginfo_t *)(uintptr_t)process_syscall_arg(
			current_process, 2);
		options = (int)process_syscall_arg(current_process, 3);
	}

	/* WSTOPPED is WUNTRACED: any bit outside this pair is EINVAL. */
	if ((unsigned int)options & ~(unsigned int)(WEXITED | WNOHANG))
		return -EINVAL;
	if ((options & WEXITED) == 0)
		return -EINVAL;

	switch (idtype)
	{
	case P_ALL:
		wait_pid = (pid_t)-1;
		break;
	case P_PID:
		if (id <= 0)
			return -EINVAL;
		wait_pid = id;
		break;
	case P_PGID:
		/*
		 * process_wait already matches pid 0 to the caller's group
		 * and pid < -1 to that process group (setpgid stores pgid).
		 */
		if (id < 0)
			return -EINVAL;
		wait_pid = (id == 0) ? (pid_t)0 : (pid_t)(-id);
		break;
	default:
		return -EINVAL;
	}

	wait_opts = (options & WNOHANG) ? WNOHANG : 0;
	/*
	 * In user mode process_wait copies the raw wait status through a
	 * user pointer. infop is that slot; the siginfo overwrites it.
	 * sys_wait4 itself would re-read idtype/infop from the frame, so
	 * the translated pid and options go to wait4_reap.
	 */
	slot = infop ? (int *)(void *)infop : NULL;
	ret = wait4_reap(wait_pid, slot, wait_opts);
	if (ret <= 0)
		return ret;
	if (!infop)
		return 0;

	raw = 0;
	if (current_process->mode == KERNEL_MODE)
		raw = *slot;
	else if (copy_from_user(&raw, slot, sizeof(raw)) != 0)
		return -EFAULT;

	memset(&info, 0, sizeof(info));
	info.si_signo = SIGCHLD;
	child_pid = (int)ret;
	if (WIFEXITED(raw))
	{
		info.si_code = CLD_EXITED;
		child_status = WEXITSTATUS(raw);
	}
	else
	{
		info.si_code = (raw & 0x80) ? CLD_DUMPED : CLD_KILLED;
		child_status = raw & 0x7f;
	}
	/*
	 * siginfo_t's union is the Linux _sigchld slot (same bytes as
	 * si_addr): si_pid at the start, si_status eight bytes later.
	 */
	memcpy(&info._sifields, &child_pid, sizeof(child_pid));
	memcpy((char *)&info._sifields + 8, &child_status,
	       sizeof(child_status));

	if (current_process->mode == KERNEL_MODE)
	{
		*infop = info;
		return 0;
	}
	if (copy_to_user(infop, &info, sizeof(info)) != 0)
		return -EFAULT;
	return 0;
}

/*
 * sys_prctl — Linux prctl(2), no_new_privs subset.
 * PR_SET_NO_NEW_PRIVS is one-way: once set it cannot be cleared, and it makes
 * every later execve ignore set-user-ID / set-group-ID bits.
 */
/* prctl, set_tid_address, rlimit, getrandom. */

int64_t sys_prctl(int option, unsigned long arg2, unsigned long arg3,
                  unsigned long arg4, unsigned long arg5)
{
  if (!current_process)
    return -ESRCH;

  switch (option)
  {
  case PR_SET_PDEATHSIG:
    if (arg2 > _NSIG)
      return -EINVAL;
    current_process->pdeathsig = (int)arg2;
    return 0;
  case PR_GET_PDEATHSIG:
    {
      int word = current_process->pdeathsig;

      if (copy_to_user((void *)(uintptr_t)arg2, &word, sizeof(word)) != 0)
        return -EFAULT;
    }
    return 0;
  case PR_SET_DUMPABLE:
    if (arg2 > 1)
      return -EINVAL;
    current_process->dumpable = (uint8_t)arg2;
    return 0;
  case PR_GET_DUMPABLE:
    return (int64_t)current_process->dumpable;
  case PR_SET_KEEPCAPS:
    if (arg2 > 1)
      return -EINVAL;
    current_process->keep_caps = (uint8_t)arg2;
    return 0;
  case PR_GET_KEEPCAPS:
    return (int64_t)current_process->keep_caps;
  case PR_SET_NAME:
    {
      char comm[sizeof(current_process->comm)];
      long n;

      memset(comm, 0, sizeof(comm));
      n = strncpy_from_user(comm, (const char *)(uintptr_t)arg2,
                            (long)sizeof(comm));
      if (n < 0)
        return n;
      comm[sizeof(comm) - 1] = '\0';
      memcpy(current_process->comm, comm, sizeof(comm));
    }
    return 0;
  case PR_GET_NAME:
    if (copy_to_user((void *)(uintptr_t)arg2, current_process->comm,
                     sizeof(current_process->comm)) != 0)
      return -EFAULT;
    return 0;
  case PR_SET_NO_NEW_PRIVS:
    if (arg2 != 1 || arg3 || arg4 || arg5)
      return -EINVAL;
    current_process->no_new_privs = 1;
    return 0;
  case PR_GET_NO_NEW_PRIVS:
    if (arg2 || arg3 || arg4 || arg5)
      return -EINVAL;
    return (int64_t)current_process->no_new_privs;
  default:
    return -EINVAL;
  }
}

int64_t sys_arch_prctl(int code, unsigned long addr)
{
  uint64_t fsbase;

  if (!current_process)
    return -ESRCH;

  if (code == ARCH_SET_FS)
  {
    /*
     * Linux do_arch_prctl_64: ARCH_SET_FS rejects addr >= TASK_SIZE
     * (-EPERM). 0 clears the base. A failed mmap/brk (-EROFS == -30)
     * used as the TLS pointer installed FS=0xffffffffffffffe2; the next
     * TCB store then #PF'd the session shell at that address.
     */
    if (addr != 0 &&
        (addr < mm_user_va_start() || addr >= mm_user_va_end()))
      return -EPERM;
    process_tls_set(current_process, (uint64_t)addr);
    set_tls((uint64_t)addr);
    return 0;
  }

  if (code == ARCH_GET_FS)
  {
    if (addr == 0)
      return -EINVAL;
    fsbase = process_tls_get(current_process);
    if (copy_to_user((void *)(uintptr_t)addr, &fsbase, sizeof(uint64_t)) != 0)
      return -EFAULT;
    return 0;
  }

  return -EINVAL;
}

/*
 * sys_set_tid_address - Linux set_tid_address(2).
 */
int64_t sys_set_tid_address(int *tidptr)
{
  if (!current_process)
    return -ESRCH;

  if (tidptr && !access_ok(tidptr, sizeof(int)))
    return -EFAULT;

  current_process->set_tid_ptr = tidptr;
  return (int64_t)current_process->task.pid;
}
/*
 * sys_getrandom - Non-cryptographic entropy (getrandom(2) subset).
 */
int64_t sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
  uint64_t seed;

  (void)flags;

  if (!current_process || !buf)
    return -EFAULT;

  if (buflen == 0)
    return 0;

  seed = clock_get_uptime_milliseconds() ^
         ((uint64_t)current_process->task.pid << 32);

  {
    uint8_t kchunk[256];
    size_t done = 0;

    while (done < buflen)
    {
      size_t chunk = buflen - done;
      size_t j;

      if (chunk > sizeof(kchunk))
        chunk = sizeof(kchunk);
      for (j = 0; j < chunk; j++)
      {
        seed = seed * 6364136223846793005ULL + 1;
        kchunk[j] = (uint8_t)(seed >> 33);
      }
      if (copy_to_user((uint8_t *)buf + done, kchunk, chunk) != 0)
        return -EFAULT;
      done += chunk;
    }
  }

  return (int64_t)buflen;
}
int64_t sys_prlimit64(pid_t pid, unsigned int resource, const void *new_limit,
                      void *old_limit)
{
  process_t *target = current_process;
  uint64_t lim[2];

  if (!current_process)
    return -ESRCH;

  if (pid != 0 && pid != (pid_t)current_process->task.pid)
    return -ESRCH;

  if (resource >= IR0_RLIM_NLIMITS)
    return -EINVAL;

  if (old_limit)
  {
    lim[0] = target->rlimits[resource].rlim_cur;
    lim[1] = target->rlimits[resource].rlim_max;
    if (copy_to_user(old_limit, lim, 16) != 0)
      return -EFAULT;
  }

  if (new_limit)
  {
    if (copy_from_user(lim, new_limit, 16) != 0)
      return -EFAULT;
    if (lim[0] > lim[1])
      return -EINVAL;
    /* Same ceiling as Linux sysctl_nr_open: nobody may exceed it. */
    if (resource == IR0_RLIMIT_NOFILE &&
	lim[1] > (uint64_t)IR0_NOFILE_CEILING)
      return -EPERM;
    if (lim[1] > target->rlimits[resource].rlim_max &&
	!ir0_cred_is_root())
      return -EPERM;
    target->rlimits[resource].rlim_cur = lim[0];
    target->rlimits[resource].rlim_max = lim[1];
  }

  return 0;
}

int64_t sys_getrlimit(unsigned int resource, void *rlim)
{
  return sys_prlimit64(0, resource, NULL, rlim);
}
