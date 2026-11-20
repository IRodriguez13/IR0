/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: Signal syscalls: rt_sigaction, rt_sigprocmask, rt_sigsuspend,
 *              kill, tkill, tgkill and rt_sigreturn (Linux kernel/signal.c
 *              syscall entry points).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <kernel/syscalls/process_syscalls.h>
#include <stddef.h>
#include <string.h>
#include <ir0/context.h>
#include <ir0/copy_user.h>
#include <ir0/sched.h>
#include <ir0/signal_irq.h>
#include <ir0/task_ops.h>
#include <ir0/tls.h>

static uint32_t rt_sigaction_mask_from_sigset(const sigset_t *set, size_t sigsetsize)
{
	uint64_t legacy64;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (!set)
			return 0;
		memcpy(&legacy64, set, sizeof(legacy64));
		return linux_sigword_to_ir0((uint32_t)legacy64);
	}
	return ir0_sigset_low32(set);
}

static void rt_sigaction_mask_to_sigset(sigset_t *set, size_t sigsetsize, uint32_t mask)
{
	uint64_t legacy64;

	if (!set)
		return;

	if (sigsetsize == sizeof(uint64_t))
	{
		legacy64 = (uint64_t)ir0_sigword_to_linux(mask);
		memcpy(set, &legacy64, sizeof(legacy64));
		return;
	}
	ir0_sigset_set_low32(set, mask);
}

/*
 * Callers pass sigsetsize=8 (_NSIG/8); user buffers are sized for the uapi layout,
 * not for IR0 internal struct sigaction.  Copy handler + flags + restorer +
 * mask64 (32 bytes), never sizeof(struct sigaction).
 */
static size_t rt_sigaction_user_copy_size(size_t sigsetsize)
{
	if (sigsetsize == sizeof(uint64_t))
		return offsetof(struct sigaction, sa_mask) + sizeof(uint64_t);
	return sizeof(struct sigaction);
}

int64_t sys_rt_sigaction(int signum, const struct sigaction *act,
			 struct sigaction *oldact, size_t sigsetsize)
{
	struct sigaction kact;
	struct sigaction kold;
	size_t user_bytes;

	if (!current_process)
		return -ESRCH;
	if (signum < 1 || signum >= _NSIG)
		return -EINVAL;
	if (signum == SIGKILL || signum == SIGSTOP)
		return -EINVAL;
	/* sigsetsize=8 (uapi compact) or sizeof(sigset_t); anything else -EINVAL. */
	if (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	user_bytes = rt_sigaction_user_copy_size(sigsetsize);

	if (oldact)
	{
		memset(&kold, 0, sizeof(kold));
		kold.sa_handler = current_process->signal_handlers[signum];
		rt_sigaction_mask_to_sigset(&kold.sa_mask, sigsetsize,
					    current_process->signal_sa_mask[signum]);
		kold.sa_flags = (unsigned long)current_process->signal_sa_flags[signum];
		kold.sa_restorer = current_process->signal_restorer[signum];
		if (copy_to_user(oldact, &kold, user_bytes) != 0)
			return -EFAULT;
	}

	if (act)
	{
		memset(&kact, 0, sizeof(kact));
		if (copy_from_user(&kact, act, user_bytes) != 0)
			return -EFAULT;
		if (register_signal_handler(signum, kact.sa_handler) != 0)
			return -EFAULT;
		current_process->signal_sa_mask[signum] =
			rt_sigaction_mask_from_sigset(&kact.sa_mask, sigsetsize) &
			~SIGNAL_UNBLOCKABLE;
		current_process->signal_sa_flags[signum] = (uint32_t)kact.sa_flags;
		current_process->signal_restorer[signum] =
			(kact.sa_flags & SA_RESTORER) ? kact.sa_restorer : NULL;
	}

	return 0;
}

int64_t sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
			   size_t sigsetsize)
{
	sigset_t kset;
	uint32_t oldmask;
	uint32_t newmask;
	uint64_t legacy64;

	if (!current_process)
		return -ESRCH;
	if (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	/*
	 * Linux sigprocmask(): read the new set, apply it, and only then
	 * report the old one, so a bad @how leaves *oldset untouched.
	 */
	oldmask = current_process->signal_mask;
	if (set)
	{
		if (sigsetsize == sizeof(uint64_t))
		{
			if (copy_from_user(&legacy64, set, sizeof(legacy64)) != 0)
				return -EFAULT;
			newmask = linux_sigword_to_ir0((uint32_t)legacy64);
		}
		else
		{
			if (copy_from_user(&kset, set, sizeof(kset)) != 0)
				return -EFAULT;
			newmask = ir0_sigset_low32(&kset);
		}
		switch (how)
		{
		case SIG_BLOCK:
			set_current_blocked(oldmask | newmask);
			break;
		case SIG_UNBLOCK:
			set_current_blocked(oldmask & ~newmask);
			break;
		case SIG_SETMASK:
			set_current_blocked(newmask);
			break;
		default:
			return -EINVAL;
		}
	}

	if (oldset)
	{
		if (sigsetsize == sizeof(uint64_t))
		{
			legacy64 = (uint64_t)ir0_sigword_to_linux(oldmask);
			if (copy_to_user(oldset, &legacy64, sizeof(legacy64)) != 0)
				return -EFAULT;
		}
		else
		{
			ir0_sigset_set_low32(&kset, oldmask);
			if (copy_to_user(oldset, &kset, sizeof(kset)) != 0)
				return -EFAULT;
		}
	}
	return 0;
}

/*
 * sys_rt_sigsuspend - Linux rt_sigsuspend(2): replace mask and wait for signal.
 * Always returns -EINTR when a non-blocked signal is pending (rt_sigsuspend(2)).
 */
int64_t sys_rt_sigsuspend(const sigset_t *mask, size_t sigsetsize)
{
	sigset_t kset;
	uint32_t saved_mask;
	size_t copy_bytes;

	if (!current_process)
		return -ESRCH;
	/*
	 * sigsetsize=8 (uapi compact) or sizeof(sigset_t), same contract as
	 * rt_sigaction above. Linux rt_sigsuspend() takes _NSIG/8 = 8, the
	 * kernel sigset width on x86-64 and arm64; the libc sigset_t is 128
	 * bytes and is never what a libc passes.
	 */
	if (!mask ||
	    (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t)))
		return -EINVAL;

	memset(&kset, 0, sizeof(kset));
	copy_bytes = (sigsetsize == sizeof(uint64_t)) ? sizeof(uint64_t)
						      : sizeof(sigset_t);
	if (copy_from_user(&kset, mask, copy_bytes) != 0)
		return -EFAULT;

	saved_mask = current_process->signal_mask;
	set_current_blocked(rt_sigaction_mask_from_sigset(&kset, sigsetsize));

	for (;;)
	{
		/*
		 * prepare_to_wait: mark BLOCKED before testing for a signal, so
		 * one sent in between sets READY instead of being lost.
		 */
		process_set_sched_state(current_process, PROCESS_BLOCKED);
		if (signal_pending(current_process))
			break;
		schedule();
	}
	process_set_sched_state(current_process, PROCESS_READY);

	/*
	 * Linux sigsuspend + set_restore_sigmask(): the handler runs with the
	 * temporary mask still installed and its frame saves the caller's;
	 * the syscall exit puts that back when no handler runs.
	 */
	current_process->saved_sigmask = saved_mask;
	current_process->restore_sigmask = 1;
	return -ERESTARTNOHAND;
}

/*
 * sys_tkill — Linux tkill(2) (x86-64 NR 200). A libc may implement raise()
 * and abort() with it; -ENOSYS there turns abort() into a crash.
 */
int64_t sys_tkill(pid_t tid, int sig)
{
	process_t *p;

	if (!current_process)
		return -ESRCH;
	if (sig < 0 || sig >= _NSIG)
		return -EINVAL;
	if (tid <= 0)
		return -EINVAL;

	p = process_find_by_pid(tid);
	if (!p)
		return -ESRCH;
	if (sig == 0)
		return 0;
	if (send_signal(tid, sig) != 0)
		return -ESRCH;
	return 0;
}

int64_t sys_tgkill(pid_t tgid, pid_t tid, int sig)
{
	process_t *p;

	if (!current_process)
		return -ESRCH;
	if (sig < 0 || sig >= _NSIG)
		return -EINVAL;

	p = process_find_by_pid(tid);
	if (!p || p->tgid != tgid)
		return -ESRCH;
	if (sig == 0)
		return 0;
	if (send_signal(tid, sig) != 0)
		return -ESRCH;
	return 0;
}

/* kill(2) permission (Linux check_kill_permission(), uid subset). */
static int kill_permitted(const process_t *target)
{
	return current_process->euid == 0 ||
	       current_process->euid == target->uid ||
	       current_process->euid == target->suid;
}

/*
 * sys_kill - Linux kill(2). pid > 0 names one process; 0 and < -1 name a
 * process group; -1 names every user process except init and the caller
 * (Linux kill_something_info()). Kernel tasks never receive signals.
 */
int64_t sys_kill(pid_t pid, int signal)
{
	process_t *target;
	process_t *scan;
	pid_t pgid;
	int count = 0;
	int sent = 0;
	int self = 0;

	if (!current_process)
		return -ESRCH;
	if (signal < 0 || signal >= _NSIG)
		return -EINVAL;

	if (pid > 0)
	{
		target = process_find_by_pid(pid);
		if (!target)
			return -ESRCH;
		if (!kill_permitted(target))
			return -EPERM;
		if (send_signal(pid, signal) != 0)
			return -ESRCH;
		return 0;
	}

	if (pid == INT32_MIN)
		return -ESRCH;
	pgid = (pid == 0) ? current_process->pgid : -pid;
	for (scan = process_list; scan; scan = scan->next)
	{
		if (scan->mode != USER_MODE)
			continue;
		if (pid == -1)
		{
			if (scan->task.pid <= 1 || scan->tgid == current_process->tgid)
				continue;
		}
		else if (scan->pgid != pgid)
			continue;
		count++;
		if (!kill_permitted(scan))
			continue;
		/* The caller last: a fatal signal to itself does not return. */
		if (scan == current_process)
		{
			self = 1;
			continue;
		}
		/* A zombie still counts and takes the signal as a no-op (Linux). */
		if (process_is_zombie(scan) || send_signal(scan->task.pid, signal) == 0)
			sent++;
	}

	/*
	 * kill(-1) succeeds once any process qualified, even if permission was
	 * refused for all of them; a process group needs one delivery.
	 */
	if (count == 0)
		return -ESRCH;
	if (self && send_signal(current_process->task.pid, signal) == 0)
		sent++;
	if (sent == 0 && pid != -1)
		return -EPERM;
	return 0;
}

/**
 * sys_sigreturn - Return from signal handler (rt_sigreturn)
 *
 * Restores CPU state saved at signal delivery. Never returns on success.
 *
 * Linux rt_sigreturn takes no argument: a restorer is just
 *   mov $__NR_rt_sigreturn, %eax; syscall
 * and the handler's return leaves the user stack pointer at our struct
 * sigframe. The frame is copied from there and from nowhere else; a frame
 * that cannot be copied in or does not validate ends in SIGSEGV.
 */
int64_t sys_sigreturn(void)
{
	struct sigframe user_frame;
	struct sigcontext restored;
	struct sigcontext *restore_ctx = &restored;
	uint64_t user_sp;

	if (!current_process)
		return -ESRCH;

	if (current_process->mode != USER_MODE)
		return 0;

	user_sp = process_syscall_sp(current_process);
	if (copy_from_user(&user_frame, (const void *)(uintptr_t)user_sp,
			   sizeof(user_frame)) != 0)
	{
		current_process->signal_pending |= SIGNAL_MASK(SIGSEGV);
		return -EFAULT;
	}
	restored = user_frame.ctx;
	arch_ucontext_import(user_frame.ucontext,
			     user_sp + offsetof(struct sigframe, ucontext),
			     &restored);
	if (!signal_sigcontext_validate_and_sanitize(restore_ctx))
	{
		/* Linux force_sig(SIGSEGV), delivered on the way out. */
		current_process->signal_pending |= SIGNAL_MASK(SIGSEGV);
		return -EFAULT;
	}

	set_current_blocked((uint32_t)user_frame.oldmask);
	current_process->signal_mask_saved_valid = 0;
	current_process->signal_frame_sp = 0;

	/*
	 * Every register comes back, rcx/r11 included, so return through the
	 * full user-state path rather than the syscall exit. The syscall frame
	 * follows the restored context.
	 */
	task_load_sigcontext(&current_process->task, restore_ctx);
	signal_fill_syscall_frame_from_sigcontext(&current_process->syscall_frame,
						  restore_ctx);
	/*
	 * Linux exit_to_user_mode() after rt_sigreturn: a signal the restored
	 * mask unblocks runs now, saving the restored context in its frame.
	 */
	do_signal(current_process, restore_ctx);
	process_apply_syscall_frame_to_task(&current_process->task,
					    &current_process->syscall_frame,
					    signal_sigcontext_return_value(restore_ctx));
	process_restore_user_task_segments(current_process);
	tls_restore_current();
	switch_to_user_task(&current_process->task);
	return 0;
}
