/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral Linux process identity for staged userspace. */

#include <stdint.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/signals.h>
#include <ir0/utsname.h>

#define EAGAIN 11
#define ECHILD 10
#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38

static int g_getpid_ok;
static int g_umask = 0022;
static uint64_t g_sigmask;
static uint64_t g_sa_handler[65];
static uint64_t g_sa_flags[65];

static int64_t early_rt_sigprocmask(struct early_syscall_context *context,
				    uint64_t how, uint64_t set, uint64_t oldset,
				    uint64_t sigsetsize)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t incoming = 0;

	if (sigsetsize != 8 && sigsetsize != 16)
		return -EINVAL;
	if (!user || !user->copy_to_user || !user->copy_from_user)
		return -ENOSYS;
	if (oldset &&
	    user->copy_to_user(oldset, &g_sigmask, 8) != 0)
		return -EFAULT;
	if (!set)
		return 0;
	if (user->copy_from_user(&incoming, set, 8) != 0)
		return -EFAULT;
	if (how == SIG_BLOCK)
		g_sigmask |= incoming;
	else if (how == SIG_UNBLOCK)
		g_sigmask &= ~incoming;
	else if (how == SIG_SETMASK)
		g_sigmask = incoming;
	else
		return -EINVAL;
	return 0;
}

static int64_t early_rt_sigaction(struct early_syscall_context *context,
				  uint64_t signo, uint64_t act, uint64_t oldact,
				  uint64_t sigsetsize)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t blob[4];

	if (signo < 1 || signo > 64)
		return -EINVAL;
	if (act && (signo == SIGKILL || signo == SIGSTOP))
		return -EINVAL;
	if (sigsetsize != 8 && sigsetsize != 16)
		return -EINVAL;
	if (!user || !user->copy_to_user || !user->copy_from_user)
		return -ENOSYS;
	if (oldact)
	{
		blob[0] = g_sa_handler[signo];
		blob[1] = g_sa_flags[signo];
		blob[2] = 0;
		blob[3] = 0;
		if (user->copy_to_user(oldact, blob, sizeof(blob)) != 0)
			return -EFAULT;
	}
	if (!act)
		return 0;
	if (user->copy_from_user(blob, act, sizeof(blob)) != 0)
		return -EFAULT;
	g_sa_handler[signo] = blob[0];
	g_sa_flags[signo] = blob[1];
	return 0;
}

static int64_t early_uname(struct early_syscall_context *context,
			   uint64_t output)
{
	struct utsname value;
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	const struct early_process_services *process =
		context && context->services ? context->services->process : 0;

	if (!user || !user->copy_to_user || !process || !process->uts_machine)
		return -ENOSYS;
	ir0_utsname_init(&value, "IR0", "ir0", "0.0.1", "early",
			 process->uts_machine);
	return user->copy_to_user(output, &value, sizeof(value)) == 0 ? 0 : -EFAULT;
}

static int64_t early_prlimit64(struct early_syscall_context *context,
			       uint64_t old_limit)
{
	uint64_t limit[2] = { UINT64_MAX, UINT64_MAX };
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;

	if (!old_limit)
		return 0;
	if (!user || !user->copy_to_user)
		return -ENOSYS;
	return user->copy_to_user(old_limit, limit, sizeof(limit)) == 0 ? 0 : -EFAULT;
}

static int64_t early_exit(struct early_syscall_context *context,
			  uint64_t status)
{
	int smoke_ok = context && context->smoke_ok && context->smoke_ok();

	if (context && context->leave_usermode)
		*context->leave_usermode = 1;
	if (context && context->services && context->services->process &&
	    context->services->process->process_exit_observed)
		context->services->process->process_exit_observed(smoke_ok);
	return (int64_t)status;
}

static int64_t early_process_syscall(void *opaque, enum ir0_syscall_id id,
				     uint64_t a0, uint64_t a1, uint64_t a2,
				     uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct early_syscall_context *context = opaque;

	(void)a4; (void)a5;
	switch (id) {
	case IR0_SYSCALL_GETPID:
		g_getpid_ok = 1;
		return 1;
	case IR0_SYSCALL_GETTID:
	case IR0_SYSCALL_SET_TID_ADDRESS:
	case IR0_SYSCALL_GETPPID:
		return 1;
	case IR0_SYSCALL_GETUID:
	case IR0_SYSCALL_GETEUID:
	case IR0_SYSCALL_GETGID:
	case IR0_SYSCALL_GETEGID:
		return 0;
	case IR0_SYSCALL_UNAME:
		return early_uname(context, a0);
	case IR0_SYSCALL_SET_ROBUST_LIST:
	case IR0_SYSCALL_PRCTL:
	case IR0_SYSCALL_RSEQ:
		return 0;
	case IR0_SYSCALL_PRLIMIT64:
		return early_prlimit64(context, a3);
	case IR0_SYSCALL_GETRANDOM:
		return a1 == 0 ? 0 : -EAGAIN;
	case IR0_SYSCALL_EXIT:
	case IR0_SYSCALL_EXIT_GROUP:
		return early_exit(context, a0);
	case IR0_SYSCALL_WAIT4:
		/*
		 * Linux wait(2): ECHILD when the caller has no unwaited-for
		 * children. Staged EL0 has not cloned yet, so this is the
		 * honest empty-reap result — not ENOSYS.
		 */
		(void)a0;
		(void)a1;
		return -ECHILD;
	case IR0_SYSCALL_GETPGID:
		return ir0_early_process_getpgid(a0);
	case IR0_SYSCALL_GETSID:
		return ir0_early_process_getsid(a0);
	case IR0_SYSCALL_SETSID:
		return ir0_early_process_setsid();
	case IR0_SYSCALL_SETPGID:
		return ir0_early_process_setpgid(a0, a1);
	case IR0_SYSCALL_UMASK:
	{
		int previous = g_umask;

		g_umask = (int)(a0 & 0777U);
		return previous;
	}
	case IR0_SYSCALL_GETGROUPS:
		return 0;
	case IR0_SYSCALL_SETGROUPS:
		return a0 == 0 ? 0 : -EINVAL;
	case IR0_SYSCALL_RT_SIGPROCMASK:
		return early_rt_sigprocmask(context, a0, a1, a2, a3);
	case IR0_SYSCALL_RT_SIGACTION:
		return early_rt_sigaction(context, a0, a1, a2, a3);
	case IR0_SYSCALL_EXECVE:
	{
		int64_t ret = early_rootfs_execve(a0, a1, a2);

		if (ret == 0 && context && context->leave_usermode)
			*context->leave_usermode = 1;
		return ret;
	}
	default:
		return -ENOSYS;
	}
}

static const enum ir0_syscall_id g_process_syscalls[] = {
	IR0_SYSCALL_GETPID, IR0_SYSCALL_GETTID, IR0_SYSCALL_SET_TID_ADDRESS,
	IR0_SYSCALL_GETUID, IR0_SYSCALL_GETEUID, IR0_SYSCALL_GETGID,
	IR0_SYSCALL_GETEGID, IR0_SYSCALL_GETPPID, IR0_SYSCALL_UNAME,
	IR0_SYSCALL_SET_ROBUST_LIST, IR0_SYSCALL_PRCTL, IR0_SYSCALL_PRLIMIT64,
	IR0_SYSCALL_RSEQ, IR0_SYSCALL_GETRANDOM, IR0_SYSCALL_WAIT4,
	IR0_SYSCALL_GETPGID, IR0_SYSCALL_GETSID, IR0_SYSCALL_SETSID,
	IR0_SYSCALL_SETPGID, IR0_SYSCALL_UMASK, IR0_SYSCALL_GETGROUPS,
	IR0_SYSCALL_SETGROUPS, IR0_SYSCALL_RT_SIGPROCMASK,
	IR0_SYSCALL_RT_SIGACTION, IR0_SYSCALL_EXECVE, IR0_SYSCALL_EXIT,
	IR0_SYSCALL_EXIT_GROUP,
};

const struct syscall_context_provider early_process_provider = {
	.ids = g_process_syscalls,
	.count = sizeof(g_process_syscalls) / sizeof(g_process_syscalls[0]),
	.handler = early_process_syscall,
};

int early_process_smoke_ok(void)
{
	return g_getpid_ok;
}
