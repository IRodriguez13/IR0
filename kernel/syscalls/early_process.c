/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral Linux process identity for staged userspace. */

#include <stdint.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/signals.h>
#include <ir0/time.h>
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

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1
#define FUTEX_PRIVATE_FLAG 128
#define FUTEX_CLOCK_REALTIME 256
#define FUTEX_CMD_MASK ~(FUTEX_PRIVATE_FLAG | FUTEX_CLOCK_REALTIME)
#define ETIMEDOUT 110
#define SS_DISABLE 2

static uint32_t g_personality;
static uint64_t g_altstack_sp;
static uint64_t g_altstack_size;
static int g_altstack_flags = SS_DISABLE;

static int64_t early_getresids(struct early_syscall_context *context,
			       uint64_t r, uint64_t e, uint64_t s)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t zero = 0;

	if (!user || !user->copy_to_user)
		return -ENOSYS;
	if (r && user->copy_to_user(r, &zero, sizeof(zero)) != 0)
		return -EFAULT;
	if (e && user->copy_to_user(e, &zero, sizeof(zero)) != 0)
		return -EFAULT;
	if (s && user->copy_to_user(s, &zero, sizeof(zero)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t early_getrlimit(struct early_syscall_context *context,
			       uint64_t rlim)
{
	uint64_t limit[2] = { UINT64_MAX, UINT64_MAX };
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;

	if (!rlim)
		return -EFAULT;
	if (!user || !user->copy_to_user)
		return -ENOSYS;
	return user->copy_to_user(rlim, limit, sizeof(limit)) == 0 ? 0 : -EFAULT;
}

static int64_t early_getrusage(struct early_syscall_context *context,
			       uint64_t usage)
{
	uint64_t zeros[18];
	unsigned i;
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;

	if (!usage)
		return -EFAULT;
	if (!user || !user->copy_to_user)
		return -ENOSYS;
	for (i = 0; i < 18; i++)
		zeros[i] = 0;
	return user->copy_to_user(usage, zeros, sizeof(zeros)) == 0 ? 0 : -EFAULT;
}

static int64_t early_sysinfo(struct early_syscall_context *context,
			     uint64_t output)
{
	const struct early_syscall_services *services_all;
	const struct early_user_services *user;
	const struct early_time_services *ops;
	struct
	{
		int64_t uptime;
		uint64_t loads[3];
		uint64_t totalram;
		uint64_t freeram;
		uint64_t sharedram;
		uint64_t bufferram;
		uint64_t totalswap;
		uint64_t freeswap;
		uint16_t procs;
		uint16_t pad;
		uint64_t totalhigh;
		uint64_t freehigh;
		uint32_t mem_unit;
	} info;
	unsigned i;
	uint64_t frequency;

	if (!context || !(services_all = context->services) ||
	    !(user = services_all->user) || !user->copy_to_user)
		return -ENOSYS;
	for (i = 0; i < sizeof(info); i++)
		((unsigned char *)&info)[i] = 0;
	ops = services_all->time;
	frequency = (ops && ops->counter_frequency) ? ops->counter_frequency() : 0;
	if (ops && ops->counter_read && frequency)
		info.uptime = (int64_t)(ops->counter_read() / frequency);
	info.totalram = 128UL * 1024UL * 1024UL;
	info.freeram = info.totalram / 2;
	info.procs = 1;
	info.mem_unit = 1;
	return user->copy_to_user(output, &info, sizeof(info)) == 0 ? 0 : -EFAULT;
}

static int64_t early_sigaltstack(struct early_syscall_context *context,
				 uint64_t uss, uint64_t uoss)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t old[3];
	uint64_t incoming[3];

	if (!user || !user->copy_to_user || !user->copy_from_user)
		return -ENOSYS;
	old[0] = g_altstack_sp;
	old[1] = (uint64_t)(uint32_t)g_altstack_flags;
	old[2] = g_altstack_size;
	if (uoss && user->copy_to_user(uoss, old, sizeof(old)) != 0)
		return -EFAULT;
	if (!uss)
		return 0;
	if (user->copy_from_user(incoming, uss, sizeof(incoming)) != 0)
		return -EFAULT;
	g_altstack_sp = incoming[0];
	g_altstack_flags = (int)(uint32_t)incoming[1];
	g_altstack_size = incoming[2];
	return 0;
}

static int64_t early_futex(struct early_syscall_context *context,
			   uint64_t uaddr, uint64_t op, uint64_t val,
			   uint64_t timeout)
{
	const struct early_syscall_services *services_all;
	const struct early_user_services *user;
	const struct early_time_services *ops;
	uint32_t observed = 0;
	uint64_t cmd = op & (uint64_t)FUTEX_CMD_MASK;

	if (!context || !(services_all = context->services) ||
	    !(user = services_all->user) || !user->copy_from_user)
		return -ENOSYS;
	if (cmd == FUTEX_WAKE)
		return 0;
	if (cmd != FUTEX_WAIT)
		return -ENOSYS;
	if (user->copy_from_user(&observed, uaddr, sizeof(observed)) != 0)
		return -EFAULT;
	if (observed != (uint32_t)val)
		return -EAGAIN;
	/*
	 * No other task can FUTEX_WAKE this waiter (clone is still
	 * ENOSYS). Honor a timeout by sleeping, then ETIMEDOUT. A NULL
	 * timeout would hang, so it also returns ETIMEDOUT.
	 */
	ops = services_all->time;
	if (timeout && ops && ops->counter_read && ops->counter_frequency &&
	    ops->cpu_relax)
	{
		struct timespec ts;
		uint64_t frequency, delta, start;

		if (user->copy_from_user(&ts, timeout, sizeof(ts)) != 0)
			return -EFAULT;
		if (ts.tv_sec < 0 || ts.tv_nsec < 0)
			return -EINVAL;
		frequency = ops->counter_frequency();
		if (frequency)
		{
			delta = (uint64_t)ts.tv_sec * frequency +
				((uint64_t)ts.tv_nsec * frequency) / 1000000000ULL;
			start = ops->counter_read();
			while (ops->counter_read() - start < delta)
				ops->cpu_relax();
		}
	}
	return -ETIMEDOUT;
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
	case IR0_SYSCALL_WAITID:
		/*
		 * Linux waitid(2): ECHILD when the caller has no children.
		 * Same empty-reap as wait4 until clone exists.
		 */
		(void)a0;
		(void)a1;
		(void)a2;
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
	case IR0_SYSCALL_KILL:
		return ir0_early_process_kill((int64_t)a0, a1);
	case IR0_SYSCALL_TKILL:
		return ir0_early_process_tkill((int64_t)a0, a1);
	case IR0_SYSCALL_TGKILL:
		return ir0_early_process_tgkill((int64_t)a0, (int64_t)a1, a2);
	case IR0_SYSCALL_GETRESUID:
	case IR0_SYSCALL_GETRESGID:
		return early_getresids(context, a0, a1, a2);
	case IR0_SYSCALL_GETRLIMIT:
		return early_getrlimit(context, a1);
	case IR0_SYSCALL_GETRUSAGE:
		return early_getrusage(context, a1);
	case IR0_SYSCALL_SYSINFO:
		return early_sysinfo(context, a0);
	case IR0_SYSCALL_PERSONALITY:
	{
		uint32_t previous = g_personality;

		if (a0 != 0xffffffffUL)
			g_personality = (uint32_t)a0;
		return previous;
	}
	case IR0_SYSCALL_SCHED_YIELD:
		return 0;
	case IR0_SYSCALL_SIGALTSTACK:
		return early_sigaltstack(context, a0, a1);
	case IR0_SYSCALL_FUTEX:
		return early_futex(context, a0, a1, a2, a3);
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
	IR0_SYSCALL_WAITID,
	IR0_SYSCALL_GETPGID, IR0_SYSCALL_GETSID, IR0_SYSCALL_SETSID,
	IR0_SYSCALL_SETPGID, IR0_SYSCALL_UMASK, IR0_SYSCALL_GETGROUPS,
	IR0_SYSCALL_SETGROUPS, IR0_SYSCALL_RT_SIGPROCMASK,
	IR0_SYSCALL_RT_SIGACTION, IR0_SYSCALL_KILL, IR0_SYSCALL_TKILL,
	IR0_SYSCALL_TGKILL, IR0_SYSCALL_GETRESUID, IR0_SYSCALL_GETRESGID,
	IR0_SYSCALL_GETRLIMIT, IR0_SYSCALL_GETRUSAGE, IR0_SYSCALL_SYSINFO,
	IR0_SYSCALL_PERSONALITY, IR0_SYSCALL_SCHED_YIELD,
	IR0_SYSCALL_SIGALTSTACK, IR0_SYSCALL_FUTEX, IR0_SYSCALL_EXECVE,
	IR0_SYSCALL_EXIT, IR0_SYSCALL_EXIT_GROUP,
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
