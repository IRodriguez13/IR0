/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral Linux process identity for staged userspace. */

#include <stdint.h>
#include <ir0/clone.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/signals.h>
#include <ir0/time.h>
#include <ir0/utsname.h>

#define EAGAIN 11
#define ECHILD 10
#define EFAULT 14
#define EINTR  4
#define EINVAL 22
#define ENOSYS 38
#define EPERM  1
#define ESRCH  3
#define E2BIG  7
#define EOPNOTSUPP 95
#define WNOHANG 1
#define WEXITED 4
#define P_ALL 0
#define P_PID 1
#define P_PGID 2
#define CHILD_NONE 0
#define CHILD_ENTER 1
#define CHILD_RUNNING 2
#define CHILD_ZOMBIE 3
#define CHILD_PARKED 4
#define EARLY_CHILD_PID 2
#define LINUX_REBOOT_MAGIC1 0xfee1deadUL
#define LINUX_REBOOT_MAGIC2 672274793UL
#define LINUX_REBOOT_MAGIC2A 85072278UL
#define LINUX_REBOOT_MAGIC2B 369367448UL
#define LINUX_REBOOT_MAGIC2C 537993216UL
#define UID_NOCHANGE 0xffffffffUL
#define PRIO_PROCESS 0

static int g_getpid_ok;
static int g_umask = 0022;
static uint32_t g_uid;
static uint32_t g_euid;
static uint32_t g_suid;
static uint32_t g_fsuid;
static uint32_t g_gid;
static uint32_t g_egid;
static uint32_t g_sgid;
static uint32_t g_fsgid;
static int32_t g_nice;
static int64_t g_current_pid = 1;
static int g_child_state;
static int g_enter_child;
static int g_resume_parent;
static uint64_t g_child_stack;
static uint64_t g_child_tls;
static uint64_t g_child_cleartid;
static int g_child_wstatus;
static int g_vfork;
static int g_vfork_exec;
static int g_yield_requested;
static int g_block_wait_ok;
static uint64_t g_parent_frame[IR0_EARLY_EL0_FRAME_WORDS];
static uint64_t g_child_frame[IR0_EARLY_EL0_FRAME_WORDS];
static int g_child_frame_valid;
static uint64_t g_sigmask;
static uint64_t g_sa_handler[65];
static uint64_t g_sa_flags[65];
static char g_comm[IR0_EARLY_TASK_COMM_LEN] = { 'i', 'n', 'i', 't', 0 };
static int g_pdeathsig;
static int g_dumpable = 1;
static int g_keepcaps;
static int g_no_new_privs;
static int g_thp_disable;
static uint64_t g_clear_tid;
static uint64_t g_capbset = ~0ULL;
static int g_sched_policy;
static int32_t g_sched_priority;
static char g_nodename[IR0_EARLY_HOSTNAME_MAX + 1] = { 'i', 'r', '0', 0 };
static char g_domainname[IR0_EARLY_HOSTNAME_MAX + 1];
static uint32_t g_cap[6] = { 0xffffffffU, 0xffffffffU, 0, 0xffffffffU,
			     0xffffffffU, 0 };
static int g_ioprio;

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
			       uint64_t r, uint64_t e, uint64_t s, int is_gid)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t ids[3];

	if (!user || !user->copy_to_user)
		return -ENOSYS;
	ids[0] = is_gid ? g_gid : g_uid;
	ids[1] = is_gid ? g_egid : g_euid;
	ids[2] = is_gid ? g_sgid : g_suid;
	if (r && user->copy_to_user(r, &ids[0], sizeof(ids[0])) != 0)
		return -EFAULT;
	if (e && user->copy_to_user(e, &ids[1], sizeof(ids[1])) != 0)
		return -EFAULT;
	if (s && user->copy_to_user(s, &ids[2], sizeof(ids[2])) != 0)
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

static int64_t early_futex_sleep_timeout(struct early_syscall_context *context,
					 uint64_t timeout, int64_t clockid)
{
	const struct early_time_services *ops;
	const struct early_user_services *user;
	struct timespec ts;
	uint64_t frequency, delta, start;

	if (clockid != 0 && clockid != 1)
		return -EINVAL;
	user = context->services->user;
	ops = context->services->time;
	if (user->copy_from_user(&ts, timeout, sizeof(ts)) != 0)
		return -EFAULT;
	if (ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
		return -EINVAL;
	if (ts.tv_sec == 0 && ts.tv_nsec == 0)
		return -ETIMEDOUT;
	if (ops && ops->counter_read && ops->counter_frequency &&
	    ops->cpu_relax)
	{
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

static int64_t early_futex_block(struct early_syscall_context *context,
				 uint64_t timeout, int64_t clockid)
{
	if (timeout)
		return early_futex_sleep_timeout(context, timeout, clockid);
	if (early_process_request_yield())
		return -EAGAIN;
	return -EAGAIN;
}

static int early_futex2_flags_ok(unsigned flags)
{
	if (flags & ~IR0_EARLY_FUTEX2_VALID_MASK)
		return 0;
	if ((flags & IR0_EARLY_FUTEX2_SIZE_MASK) != IR0_EARLY_FUTEX2_SIZE_U32)
		return 0;
	return 1;
}

static int64_t early_futex2_unsupported(unsigned flags)
{
	if (flags & (IR0_EARLY_FUTEX2_NUMA | IR0_EARLY_FUTEX2_MPOL))
		return -EOPNOTSUPP;
	return 0;
}

static int64_t early_futex_wait_u32(struct early_syscall_context *context,
				    uint64_t uaddr, uint32_t val,
				    uint64_t timeout, int64_t clockid)
{
	const struct early_user_services *user;
	uint32_t observed = 0;

	user = context->services->user;
	if (!uaddr || user->copy_from_user(&observed, uaddr, sizeof(observed)) != 0)
		return -EFAULT;
	if (observed != val)
		return -EAGAIN;
	return early_futex_block(context, timeout, clockid);
}

static int64_t early_futex(struct early_syscall_context *context,
			   uint64_t uaddr, uint64_t op, uint64_t val,
			   uint64_t timeout)
{
	const struct early_syscall_services *services_all;
	uint64_t cmd = op & (uint64_t)FUTEX_CMD_MASK;

	if (!context || !(services_all = context->services) ||
	    !services_all->user || !services_all->user->copy_from_user)
		return -ENOSYS;
	if (cmd == FUTEX_WAKE)
		return 0;
	if (cmd != FUTEX_WAIT)
		return -ENOSYS;
	return early_futex_wait_u32(context, uaddr, (uint32_t)val, timeout, 0);
}

static int64_t early_futex_waitv(struct early_syscall_context *context,
				 uint64_t waiters, unsigned nr,
				 unsigned flags, uint64_t timeout,
				 int64_t clockid)
{
	const struct early_user_services *user;
	unsigned i;

	if (!context || !context->services || !context->services->user ||
	    !context->services->user->copy_from_user)
		return -ENOSYS;
	if (flags)
		return -EINVAL;
	if (!nr || nr > IR0_EARLY_FUTEX_WAITV_MAX || !waiters)
		return -EINVAL;
	user = context->services->user;
	for (i = 0; i < nr; i++)
	{
		uint64_t blob[3];
		unsigned wflags;
		int64_t ret;
		uint32_t observed;

		if (user->copy_from_user(blob,
					 waiters + (uint64_t)i *
						   IR0_EARLY_FUTEX_WAITV_BYTES,
					 sizeof(blob)) != 0)
			return -EFAULT;
		wflags = (unsigned)(uint32_t)blob[2];
		if ((uint32_t)(blob[2] >> 32) != 0)
			return -EINVAL;
		if (!early_futex2_flags_ok(wflags))
			return -EINVAL;
		ret = early_futex2_unsupported(wflags);
		if (ret < 0)
			return ret;
		if (!blob[1] ||
		    user->copy_from_user(&observed, blob[1],
					 sizeof(observed)) != 0)
			return -EFAULT;
		if (observed != (uint32_t)blob[0])
			return -EAGAIN;
	}
	return early_futex_block(context, timeout, clockid);
}

static int64_t early_futex_wake2(struct early_syscall_context *context,
				 uint64_t uaddr, uint64_t mask, int nr,
				 unsigned flags)
{
	uint32_t observed;

	(void)mask;
	(void)nr;
	if (!context || !context->services || !context->services->user ||
	    !context->services->user->copy_from_user)
		return -ENOSYS;
	if (!early_futex2_flags_ok(flags))
		return -EINVAL;
	if (early_futex2_unsupported(flags) < 0)
		return -EOPNOTSUPP;
	if (!uaddr ||
	    context->services->user->copy_from_user(&observed, uaddr,
						    sizeof(observed)) != 0)
		return -EFAULT;
	(void)observed;
	return 0;
}

static int64_t early_futex_wait2(struct early_syscall_context *context,
				 uint64_t uaddr, uint64_t val, uint64_t mask,
				 unsigned flags, uint64_t timeout,
				 int64_t clockid)
{
	int64_t ret;

	(void)mask;
	if (!context || !context->services || !context->services->user)
		return -ENOSYS;
	if (!early_futex2_flags_ok(flags))
		return -EINVAL;
	ret = early_futex2_unsupported(flags);
	if (ret < 0)
		return ret;
	return early_futex_wait_u32(context, uaddr, (uint32_t)val, timeout,
				    clockid);
}

static int64_t early_futex_requeue(struct early_syscall_context *context,
				   uint64_t waiters, unsigned flags, int nr_wake,
				   int nr_requeue)
{
	uint64_t blob[6];
	unsigned i;
	int64_t ret;

	(void)nr_wake;
	(void)nr_requeue;
	if (!context || !context->services || !context->services->user ||
	    !context->services->user->copy_from_user)
		return -ENOSYS;
	if (flags || !waiters)
		return -EINVAL;
	if (context->services->user->copy_from_user(blob, waiters,
						    sizeof(blob)) != 0)
		return -EFAULT;
	for (i = 0; i < 2; i++)
	{
		unsigned wflags = (unsigned)(uint32_t)blob[i * 3 + 2];

		if ((uint32_t)(blob[i * 3 + 2] >> 32) != 0)
			return -EINVAL;
		if (!early_futex2_flags_ok(wflags))
			return -EINVAL;
		ret = early_futex2_unsupported(wflags);
		if (ret < 0)
			return ret;
		if (!blob[i * 3 + 1])
			return -EFAULT;
	}
	if ((uint32_t)blob[2] != (uint32_t)blob[5])
		return -EINVAL;
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
	ir0_utsname_init(&value, "IR0", g_nodename, "0.0.1", "early",
			 process->uts_machine);
	ir0_utsname_copy_field(value.domainname, g_domainname);
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

static int early_child_exists(void)
{
	return g_child_state != CHILD_NONE;
}

static int early_pid_alive(int64_t pid)
{
	if (pid == g_current_pid || pid == 1)
		return 1;
	return pid == EARLY_CHILD_PID && early_child_exists();
}

static int early_pid_exists(int64_t pid)
{
	if (pid == 0)
		return 1;
	return early_pid_alive(pid);
}

static int64_t early_write_pid32(struct early_syscall_context *context,
				 uint64_t dest, uint32_t pid)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;

	if (!dest)
		return 0;
	if (!user || !user->copy_to_user)
		return -EFAULT;
	return user->copy_to_user(dest, &pid, sizeof(pid)) == 0 ? 0 : -EFAULT;
}

static int64_t early_clone(struct early_syscall_context *context, uint64_t flags,
			   uint64_t stack, uint64_t parent_tid, uint64_t tls,
			   uint64_t child_tid, uint64_t pidfd_ptr, int from_clone3)
{
	int64_t ret;

	if (flags & CLONE_INTO_CGROUP)
		return -EOPNOTSUPP;
	if (!ir0_early_clone_flags_ok(flags))
		return (flags & CLONE_VM) ? -EINVAL : -ENOSYS;
	if (!from_clone3 && (flags & CLONE_PIDFD) &&
	    (flags & CLONE_PARENT_SETTID))
		return -EINVAL;
	if (early_child_exists())
		return -EAGAIN;
	g_vfork = (flags & CLONE_VFORK) != 0;
	g_vfork_exec = 0;
	g_yield_requested = 0;
	g_child_frame_valid = 0;
	g_child_stack = stack;
	g_child_tls = (flags & CLONE_SETTLS) ? tls : 0;
	g_child_cleartid = (flags & CLONE_CHILD_CLEARTID) ? child_tid : 0;
	g_child_wstatus = 0;
	g_enter_child = 1;
	g_resume_parent = 0;
	g_child_state = CHILD_ENTER;
	if (flags & CLONE_PARENT_SETTID)
	{
		ret = early_write_pid32(context, parent_tid, EARLY_CHILD_PID);
		if (ret < 0)
			goto fail;
	}
	if (flags & CLONE_CHILD_SETTID)
	{
		ret = early_write_pid32(context, child_tid, EARLY_CHILD_PID);
		if (ret < 0)
			goto fail;
	}
	if (flags & CLONE_PIDFD)
	{
		int32_t fd;
		const struct early_user_services *user =
			context && context->services ? context->services->user : 0;
		uint64_t dest = from_clone3 ? pidfd_ptr : parent_tid;

		ret = early_rootfs_pidfd_open(EARLY_CHILD_PID, 0);
		if (ret < 0)
			goto fail;
		fd = (int32_t)ret;
		if (!dest || !user || !user->copy_to_user ||
		    user->copy_to_user(dest, &fd, sizeof(fd)) != 0)
		{
			(void)early_rootfs_close(fd);
			ret = -EFAULT;
			goto fail;
		}
	}
	return EARLY_CHILD_PID;

fail:
	g_enter_child = 0;
	g_child_state = CHILD_NONE;
	g_child_cleartid = 0;
	g_vfork = 0;
	return ret;
}

static int64_t early_clone3(struct early_syscall_context *context, uint64_t uargs,
			    uint64_t size)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t args[11];
	uint8_t extra;
	uint64_t flags;
	uint64_t stack;
	uint64_t stack_size;
	uint64_t off;

	if (size < IR0_EARLY_CLONE_ARGS_VER0)
		return -EINVAL;
	if (size > 4096UL)
		return -E2BIG;
	if (!uargs || !user || !user->copy_from_user)
		return -EFAULT;
	args[0] = args[1] = args[2] = args[3] = args[4] = args[5] = args[6] =
		args[7] = args[8] = args[9] = args[10] = 0;
	{
		uint64_t copy = size < IR0_EARLY_CLONE_ARGS_VER2 ?
					size :
					IR0_EARLY_CLONE_ARGS_VER2;

		if (user->copy_from_user(args, uargs, copy) != 0)
			return -EFAULT;
	}
	if (size > IR0_EARLY_CLONE_ARGS_VER2)
	{
		for (off = IR0_EARLY_CLONE_ARGS_VER2; off < size; off++)
		{
			if (user->copy_from_user(&extra, uargs + off, 1) != 0)
				return -EFAULT;
			if (extra)
				return -E2BIG;
		}
	}
	flags = args[0];
	if (flags & IR0_EARLY_CSIGNAL)
		return -EINVAL;
	if (args[4] > 64)
		return -EINVAL;
	if (args[9])
		return -EOPNOTSUPP;
	flags |= args[4] & IR0_EARLY_CSIGNAL;
	stack = args[5];
	stack_size = args[6];
	if ((flags & CLONE_VM) != 0 && (!stack || !stack_size))
		return -EINVAL;
	if (stack_size)
		stack += stack_size;
	{
		int64_t ret = early_clone(context, flags, stack, args[3], args[7],
					  args[2], args[1], 1);

		if (ret == EARLY_CHILD_PID && context && context->services &&
		    context->services->time && context->services->time->note)
			context->services->time->note(EARLY_SYSCALL_CLONE3_OK);
		return ret;
	}
}

static int64_t early_wait4(struct early_syscall_context *context, int64_t pid,
			   uint64_t status, uint64_t options)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	int32_t wstatus;

	if (!early_child_exists())
		return -ECHILD;
	if (pid > 0 && pid != EARLY_CHILD_PID)
		return -ECHILD;
	if (pid < -1)
		return -ECHILD;
	if (g_child_state != CHILD_ZOMBIE)
	{
		if (options & WNOHANG)
			return 0;
		if (early_process_request_yield())
			return -EAGAIN;
		return -EAGAIN;
	}
	wstatus = g_child_wstatus;
	if (status)
	{
		if (!user || !user->copy_to_user)
			return -EFAULT;
		if (user->copy_to_user(status, &wstatus, sizeof(wstatus)) != 0)
			return -EFAULT;
	}
	g_child_state = CHILD_NONE;
	g_child_cleartid = 0;
	if (context && context->services && context->services->process &&
	    context->services->process->clone_reaped)
		context->services->process->clone_reaped();
	return EARLY_CHILD_PID;
}

static int64_t early_waitid(struct early_syscall_context *context,
			    uint64_t idtype, uint64_t id, uint64_t infop,
			    uint64_t options)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint8_t info[128];
	uint32_t *words;
	int32_t exit_code;
	int64_t pid;
	unsigned int i;

	if ((options & WEXITED) == 0)
		return -EINVAL;
	if (idtype == P_PID)
		pid = (int64_t)id;
	else if (idtype == P_ALL || idtype == P_PGID)
		pid = -1;
	else
		return -EINVAL;
	exit_code = (g_child_wstatus >> 8) & 0xff;
	pid = early_wait4(context, pid, 0, options);
	if (pid <= 0)
		return pid;
	if (!infop)
		return 0;
	if (!user || !user->copy_to_user)
		return -EFAULT;
	for (i = 0; i < sizeof(info); i++)
		info[i] = 0;
	words = (uint32_t *)(void *)info;
	words[0] = SIGCHLD;
	words[2] = 1; /* CLD_EXITED */
	words[4] = EARLY_CHILD_PID;
	words[6] = (uint32_t)exit_code;
	return user->copy_to_user(infop, info, sizeof(info)) == 0 ? 0 : -EFAULT;
}

static int64_t early_getrandom(struct early_syscall_context *context,
			       uint64_t buffer, uint64_t length)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	const struct early_time_services *time = context && context->services ?
		context->services->time : 0;
	uint8_t scratch[64];
	uint64_t stamp;
	uint64_t done = 0;

	if (length == 0)
		return 0;
	if (!user || !user->copy_to_user || !time || !time->counter_read)
		return -EAGAIN;
	stamp = time->counter_read();
	while (done < length)
	{
		uint64_t chunk = length - done;
		uint64_t i;

		if (chunk > sizeof(scratch))
			chunk = sizeof(scratch);
		for (i = 0; i < chunk; i++)
		{
			stamp = stamp * 6364136223846793005ULL + (done + i + 1);
			scratch[i] = (uint8_t)(stamp >> 33);
		}
		if (user->copy_to_user(buffer + done, scratch, chunk) != 0)
			return done ? (int64_t)done : -EFAULT;
		done += chunk;
	}
	return (int64_t)done;
}

static int64_t early_reboot(struct early_syscall_context *context,
			    uint64_t magic1, uint64_t magic2)
{
	if (magic1 != LINUX_REBOOT_MAGIC1)
		return -EINVAL;
	if (magic2 != LINUX_REBOOT_MAGIC2 && magic2 != LINUX_REBOOT_MAGIC2A &&
	    magic2 != LINUX_REBOOT_MAGIC2B && magic2 != LINUX_REBOOT_MAGIC2C)
		return -EINVAL;
	if (context && context->leave_usermode)
		*context->leave_usermode = 1;
	return 0;
}

static int64_t early_exit(struct early_syscall_context *context,
			  uint64_t status)
{
	int smoke_ok;

	if (g_current_pid == EARLY_CHILD_PID)
	{
		if (g_child_cleartid)
			(void)early_write_pid32(context, g_child_cleartid, 0);
		g_child_wstatus = (int)((status & 0xffUL) << 8);
		g_child_state = CHILD_ZOMBIE;
		g_vfork = 0;
		g_vfork_exec = 0;
		g_yield_requested = 0;
		g_resume_parent = 1;
		g_current_pid = 1;
		if (context && context->leave_usermode)
			*context->leave_usermode = 1;
		return (int64_t)status;
	}

	smoke_ok = context && context->smoke_ok && context->smoke_ok();
	if (context && context->leave_usermode)
		*context->leave_usermode = 1;
	if (context && context->services && context->services->process &&
	    context->services->process->process_exit_observed)
		context->services->process->process_exit_observed(smoke_ok);
	return (int64_t)status;
}

static int64_t early_prctl(struct early_syscall_context *context, uint64_t option,
			   uint64_t arg2, uint64_t arg3, uint64_t arg4,
			   uint64_t arg5)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	char comm[IR0_EARLY_TASK_COMM_LEN];
	int32_t word;
	uint64_t addr;
	unsigned i;

	(void)arg4;
	(void)arg5;
	switch (option)
	{
	case IR0_EARLY_PR_SET_PDEATHSIG:
		if (arg2 > 64)
			return -EINVAL;
		g_pdeathsig = (int)arg2;
		return 0;
	case IR0_EARLY_PR_GET_PDEATHSIG:
		word = g_pdeathsig;
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(arg2, &word, sizeof(word)) != 0)
			return -EFAULT;
		return 0;
	case IR0_EARLY_PR_GET_DUMPABLE:
		return g_dumpable;
	case IR0_EARLY_PR_SET_DUMPABLE:
		if (arg2 > 1)
			return -EINVAL;
		g_dumpable = (int)arg2;
		return 0;
	case IR0_EARLY_PR_GET_KEEPCAPS:
		return g_keepcaps;
	case IR0_EARLY_PR_SET_KEEPCAPS:
		if (arg2 > 1)
			return -EINVAL;
		g_keepcaps = (int)arg2;
		return 0;
	case IR0_EARLY_PR_SET_NAME:
		if (!user || !user->copy_from_user)
			return -EFAULT;
		for (i = 0; i < IR0_EARLY_TASK_COMM_LEN; i++)
			comm[i] = 0;
		for (i = 0; i < IR0_EARLY_TASK_COMM_LEN - 1; i++)
		{
			if (user->copy_from_user(&comm[i], arg2 + i, 1) != 0)
				return -EFAULT;
			if (comm[i] == 0)
				break;
		}
		comm[IR0_EARLY_TASK_COMM_LEN - 1] = 0;
		for (i = 0; i < IR0_EARLY_TASK_COMM_LEN; i++)
			g_comm[i] = comm[i];
		return 0;
	case IR0_EARLY_PR_GET_NAME:
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(arg2, g_comm, IR0_EARLY_TASK_COMM_LEN) != 0)
			return -EFAULT;
		return 0;
	case IR0_EARLY_PR_GET_SECCOMP:
		return 0;
	case IR0_EARLY_PR_CAPBSET_READ:
		if (arg2 > 63)
			return -EINVAL;
		return (g_capbset >> arg2) & 1ULL;
	case IR0_EARLY_PR_CAPBSET_DROP:
		if (arg2 > 63)
			return -EINVAL;
		g_capbset &= ~(1ULL << arg2);
		return 0;
	case IR0_EARLY_PR_SET_NO_NEW_PRIVS:
		if (arg2 != 1)
			return -EINVAL;
		g_no_new_privs = 1;
		return 0;
	case IR0_EARLY_PR_GET_NO_NEW_PRIVS:
		return g_no_new_privs;
	case IR0_EARLY_PR_GET_TID_ADDRESS:
		addr = g_clear_tid;
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(arg2, &addr, sizeof(addr)) != 0)
			return -EFAULT;
		return 0;
	case IR0_EARLY_PR_SET_THP_DISABLE:
		if (arg2 > 1)
			return -EINVAL;
		g_thp_disable = (int)arg2;
		return 0;
	case IR0_EARLY_PR_GET_THP_DISABLE:
		return g_thp_disable;
	case IR0_EARLY_PR_SET_PTRACER:
		return 0;
	case IR0_EARLY_PR_SET_VMA:
		if (arg2 != IR0_EARLY_PR_SET_VMA_ANON_NAME)
			return -EINVAL;
		(void)arg3;
		return 0;
	default:
		return -EINVAL;
	}
}

static int64_t early_getcpu(struct early_syscall_context *context, uint64_t cpu,
			    uint64_t node)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t zero = 0;

	if (cpu)
	{
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(cpu, &zero, sizeof(zero)) != 0)
			return -EFAULT;
	}
	if (node)
	{
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(node, &zero, sizeof(zero)) != 0)
			return -EFAULT;
	}
	return 0;
}

static int64_t early_sched_setparam(struct early_syscall_context *context,
				    int64_t pid, uint64_t param)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	int32_t prio;

	if (!early_pid_exists(pid))
		return -ESRCH;
	if (!user || !user->copy_from_user ||
	    user->copy_from_user(&prio, param, sizeof(prio)) != 0)
		return -EFAULT;
	if (prio != 0)
		return -EINVAL;
	g_sched_priority = 0;
	return 0;
}

static int64_t early_sched_getparam(struct early_syscall_context *context,
				    int64_t pid, uint64_t param)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;

	if (!early_pid_exists(pid))
		return -ESRCH;
	if (!user || !user->copy_to_user ||
	    user->copy_to_user(param, &g_sched_priority, sizeof(g_sched_priority)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t early_sched_setscheduler(struct early_syscall_context *context,
					int64_t pid, int policy, uint64_t param)
{
	int64_t ret;

	if (ir0_early_sched_policy_rt(policy))
		return -EPERM;
	if (!ir0_early_sched_policy_ok(policy))
		return -EINVAL;
	ret = early_sched_setparam(context, pid, param);
	if (ret < 0)
		return ret;
	g_sched_policy = policy;
	return 0;
}

static int64_t early_sched_getscheduler(int64_t pid)
{
	if (!early_pid_exists(pid))
		return -ESRCH;
	return g_sched_policy;
}

static int64_t early_sched_setaffinity(struct early_syscall_context *context,
				       int64_t pid, uint64_t len, uint64_t mask)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t bits = 0;

	if (!early_pid_exists(pid))
		return -ESRCH;
	if (!len || (len & 7UL))
		return -EINVAL;
	if (!user || !user->copy_from_user)
		return -EFAULT;
	if (user->copy_from_user(&bits, mask, 8) != 0)
		return -EFAULT;
	if ((bits & 1ULL) == 0)
		return -EINVAL;
	return 0;
}

static int64_t early_sched_getaffinity(struct early_syscall_context *context,
				       int64_t pid, uint64_t len, uint64_t mask)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint64_t bits = 1;

	if (!early_pid_exists(pid))
		return -ESRCH;
	if (len < 8 || (len & 7UL))
		return -EINVAL;
	if (!user || !user->copy_to_user ||
	    user->copy_to_user(mask, &bits, 8) != 0)
		return -EFAULT;
	return 8;
}

static int64_t early_process_vm(struct early_syscall_context *context,
			       int64_t pid, uint64_t lvec, uint64_t liovcnt,
			       uint64_t rvec, uint64_t riovcnt, uint64_t flags,
			       int write_remote)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	const struct early_time_services *time = context && context->services ?
		context->services->time : 0;
	uint64_t li = 0;
	uint64_t ri = 0;
	uint64_t lbase = 0;
	uint64_t llen = 0;
	uint64_t rbase = 0;
	uint64_t rlen = 0;
	int64_t total = 0;
	uint8_t tmp[256];

	if (flags)
		return -EINVAL;
	if (liovcnt > IR0_EARLY_IOV_MAX || riovcnt > IR0_EARLY_IOV_MAX)
		return -EINVAL;
	if (!early_pid_exists(pid))
		return -ESRCH;
	if (!user || !user->copy_from_user || !user->copy_to_user)
		return -EFAULT;
	while (1)
	{
		uint64_t n;
		uint64_t vec[2];
		int64_t copied;

		while (llen == 0 && li < liovcnt)
		{
			if (user->copy_from_user(vec, lvec + li * IR0_EARLY_IOV_BYTES,
						 sizeof(vec)) != 0)
				return total ? total : -EFAULT;
			lbase = vec[0];
			llen = vec[1];
			li++;
		}
		while (rlen == 0 && ri < riovcnt)
		{
			if (user->copy_from_user(vec, rvec + ri * IR0_EARLY_IOV_BYTES,
						 sizeof(vec)) != 0)
				return total ? total : -EFAULT;
			rbase = vec[0];
			rlen = vec[1];
			ri++;
		}
		if (llen == 0 || rlen == 0)
			break;
		n = llen < rlen ? llen : rlen;
		if (n > sizeof(tmp))
			n = sizeof(tmp);
		if (write_remote)
			copied = user->copy_from_user(tmp, lbase, n);
		else
			copied = user->copy_from_user(tmp, rbase, n);
		if (copied != 0)
			return total ? total : -EFAULT;
		if (write_remote)
			copied = user->copy_to_user(rbase, tmp, n);
		else
			copied = user->copy_to_user(lbase, tmp, n);
		if (copied != 0)
			return total ? total : -EFAULT;
		lbase += n;
		rbase += n;
		llen -= n;
		rlen -= n;
		total += (int64_t)n;
	}
	if (total > 0 && time && time->note)
		time->note(EARLY_SYSCALL_PROCESS_VM_OK);
	return total;
}

static int64_t early_membarrier(int cmd, int flags)
{
	if (flags)
		return -EINVAL;
	if (cmd == IR0_EARLY_MEMBARRIER_CMD_QUERY)
		return IR0_EARLY_MEMBARRIER_SUPPORTED;
	if (cmd == IR0_EARLY_MEMBARRIER_CMD_GLOBAL ||
	    cmd == IR0_EARLY_MEMBARRIER_CMD_GLOBAL_EXPEDITED ||
	    cmd == IR0_EARLY_MEMBARRIER_CMD_REGISTER_GLOBAL_EXPEDITED ||
	    cmd == IR0_EARLY_MEMBARRIER_CMD_PRIVATE_EXPEDITED ||
	    cmd == IR0_EARLY_MEMBARRIER_CMD_REGISTER_PRIVATE_EXPEDITED)
	{
		__asm__ volatile("" ::: "memory");
		return 0;
	}
	return -EINVAL;
}

static int64_t early_set_uts_field(struct early_syscall_context *context,
				   uint64_t name, uint64_t len, char *dst)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	unsigned i;

	if (!len || len > IR0_EARLY_HOSTNAME_MAX)
		return -EINVAL;
	if (!user || !user->copy_from_user)
		return -EFAULT;
	for (i = 0; i <= IR0_EARLY_HOSTNAME_MAX; i++)
		dst[i] = 0;
	if (user->copy_from_user(dst, name, len) != 0)
		return -EFAULT;
	dst[len] = 0;
	return 0;
}

static int64_t early_times(struct early_syscall_context *context, uint64_t buf)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	const struct early_time_services *time = context && context->services ?
		context->services->time : 0;
	uint64_t tms[4];
	uint64_t freq;
	uint64_t ticks;

	tms[0] = tms[1] = tms[2] = tms[3] = 0;
	freq = (time && time->counter_frequency) ? time->counter_frequency() : 0;
	ticks = (time && time->counter_read) ? time->counter_read() : 0;
	if (freq)
		ticks = ticks / (freq / 100ULL ? freq / 100ULL : 1ULL);
	if (buf)
	{
		if (!user || !user->copy_to_user ||
		    user->copy_to_user(buf, tms, sizeof(tms)) != 0)
			return -EFAULT;
	}
	return (int64_t)ticks;
}

static int64_t early_capget(struct early_syscall_context *context, uint64_t header,
			    uint64_t data)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t hdr[2];
	uint32_t ver;

	if (!user || !user->copy_from_user || !user->copy_to_user)
		return -EFAULT;
	if (user->copy_from_user(hdr, header, sizeof(hdr)) != 0)
		return -EFAULT;
	ver = hdr[0];
	if (ver != IR0_EARLY_LINUX_CAP_VERSION_3)
	{
		hdr[0] = IR0_EARLY_LINUX_CAP_VERSION_3;
		(void)user->copy_to_user(header, hdr, sizeof(hdr));
		return -EINVAL;
	}
	if ((int32_t)hdr[1] != 0 && !early_pid_exists((int64_t)(int32_t)hdr[1]))
		return -ESRCH;
	if (data && user->copy_to_user(data, g_cap, sizeof(g_cap)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t early_capset(struct early_syscall_context *context, uint64_t header,
			    uint64_t data)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t hdr[2];
	uint32_t incoming[6];

	if (!user || !user->copy_from_user)
		return -EFAULT;
	if (user->copy_from_user(hdr, header, sizeof(hdr)) != 0)
		return -EFAULT;
	if (hdr[0] != IR0_EARLY_LINUX_CAP_VERSION_3)
		return -EINVAL;
	if ((int32_t)hdr[1] != 0 && !early_pid_exists((int64_t)(int32_t)hdr[1]))
		return -ESRCH;
	if (!data)
		return -EFAULT;
	if (user->copy_from_user(incoming, data, sizeof(incoming)) != 0)
		return -EFAULT;
	{
		unsigned i;

		for (i = 0; i < 6; i++)
			g_cap[i] = incoming[i];
	}
	return 0;
}

static int64_t early_ioprio_set(int which, int64_t who, int ioprio)
{
	int class = ioprio >> IR0_EARLY_IOPRIO_CLASS_SHIFT;

	if (which < IR0_EARLY_IOPRIO_WHO_PROCESS ||
	    which > IR0_EARLY_IOPRIO_WHO_USER)
		return -EINVAL;
	if (who != 0 && !early_pid_exists(who) &&
	    which == IR0_EARLY_IOPRIO_WHO_PROCESS)
		return -ESRCH;
	if (class == IR0_EARLY_IOPRIO_CLASS_RT)
		return -EPERM;
	if (class != IR0_EARLY_IOPRIO_CLASS_NONE &&
	    class != IR0_EARLY_IOPRIO_CLASS_BE &&
	    class != IR0_EARLY_IOPRIO_CLASS_IDLE)
		return -EINVAL;
	g_ioprio = ioprio;
	return 0;
}

static int64_t early_ioprio_get(int which, int64_t who)
{
	if (which < IR0_EARLY_IOPRIO_WHO_PROCESS ||
	    which > IR0_EARLY_IOPRIO_WHO_USER)
		return -EINVAL;
	if (who != 0 && !early_pid_exists(who) &&
	    which == IR0_EARLY_IOPRIO_WHO_PROCESS)
		return -ESRCH;
	return g_ioprio;
}

static int64_t early_sched_priority_limit(int policy, int max_not_min)
{
	if (ir0_early_sched_policy_ok(policy))
		return 0;
	if (ir0_early_sched_policy_rt(policy))
		return max_not_min ? 99 : 1;
	return -EINVAL;
}

static int64_t early_sched_rr_interval(struct early_syscall_context *context,
				       int64_t pid, uint64_t out)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	int64_t ts[2];

	if (!early_pid_exists(pid))
		return -ESRCH;
	ts[0] = 0;
	ts[1] = 100000000;
	if (!user || !user->copy_to_user ||
	    user->copy_to_user(out, ts, sizeof(ts)) != 0)
		return -EFAULT;
	return 0;
}

static int64_t early_sched_getattr(struct early_syscall_context *context,
				   int64_t pid, uint64_t uattr, uint64_t usize)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint8_t blob[IR0_EARLY_SCHED_ATTR_SIZE_VER1];
	uint32_t *w;
	unsigned i;

	if (!early_pid_exists(pid))
		return -ESRCH;
	if (usize < IR0_EARLY_SCHED_ATTR_SIZE_VER0)
		return -EINVAL;
	for (i = 0; i < sizeof(blob); i++)
		blob[i] = 0;
	w = (uint32_t *)(void *)blob;
	w[0] = IR0_EARLY_SCHED_ATTR_SIZE_VER1;
	w[1] = (uint32_t)g_sched_policy;
	w[4] = (uint32_t)g_nice;
	if (!user || !user->copy_to_user ||
	    user->copy_to_user(uattr, blob,
			       usize > sizeof(blob) ? sizeof(blob) : usize) != 0)
		return -EFAULT;
	return 0;
}

static int64_t early_sched_setattr(struct early_syscall_context *context,
				   int64_t pid, uint64_t uattr, uint64_t flags)
{
	const struct early_user_services *user = context && context->services ?
		context->services->user : 0;
	uint32_t blob[12];
	int policy;

	if (flags)
		return -EINVAL;
	if (!early_pid_exists(pid))
		return -ESRCH;
	if (!user || !user->copy_from_user ||
	    user->copy_from_user(blob, uattr, IR0_EARLY_SCHED_ATTR_SIZE_VER0) != 0)
		return -EFAULT;
	if (blob[0] < IR0_EARLY_SCHED_ATTR_SIZE_VER0)
		return -EINVAL;
	policy = (int)blob[1];
	if (ir0_early_sched_policy_rt(policy))
		return -EPERM;
	if (!ir0_early_sched_policy_ok(policy))
		return -EINVAL;
	if (blob[4])
		return -EINVAL;
	g_sched_policy = policy;
	return 0;
}

static int64_t early_kcmp(int64_t pid1, int64_t pid2, int type, uint64_t idx1,
			  uint64_t idx2)
{
	if (!early_pid_alive(pid1) || !early_pid_alive(pid2))
		return -ESRCH;
	if (type == IR0_EARLY_KCMP_FILE)
		return early_rootfs_kcmp_file((int)idx1, (int)idx2);
	if (type == IR0_EARLY_KCMP_VM || type == IR0_EARLY_KCMP_FILES ||
	    type == IR0_EARLY_KCMP_FS || type == IR0_EARLY_KCMP_SIGHAND ||
	    type == IR0_EARLY_KCMP_IO || type == IR0_EARLY_KCMP_SYSVSEM)
		return 0;
	return -EINVAL;
}

static int64_t early_pidfd_open(int64_t pid, unsigned flags)
{
	if (pid < 0)
		return -EINVAL;
	if (!early_pid_alive(pid))
		return -ESRCH;
	return early_rootfs_pidfd_open((int32_t)pid, flags);
}

static int64_t early_pidfd_send_signal(int fd, uint64_t sig, uint64_t info,
				       unsigned flags)
{
	int32_t pid;

	if (flags)
		return -EINVAL;
	if (sig > 64)
		return -EINVAL;
	if (info)
		return -EPERM;
	pid = early_rootfs_pidfd_pid(fd);
	if (pid < 0)
		return pid;
	if (!early_pid_alive(pid))
		return -ESRCH;
	return 0;
}

static int64_t early_pidfd_getfd(int pidfd, int target, unsigned flags)
{
	int32_t pid;

	if (flags)
		return -EINVAL;
	pid = early_rootfs_pidfd_pid(pidfd);
	if (pid < 0)
		return pid;
	if (!early_pid_alive(pid))
		return -ESRCH;
	return early_rootfs_pidfd_getfd(target);
}

static int64_t early_process_syscall(void *opaque, enum ir0_syscall_id id,
				     uint64_t a0, uint64_t a1, uint64_t a2,
				     uint64_t a3, uint64_t a4, uint64_t a5)
{
	struct early_syscall_context *context = opaque;

	switch (id) {
	case IR0_SYSCALL_GETPID:
		g_getpid_ok = 1;
		return g_current_pid;
	case IR0_SYSCALL_GETTID:
		return g_current_pid;
	case IR0_SYSCALL_SET_TID_ADDRESS:
		g_clear_tid = a0;
		return g_current_pid;
	case IR0_SYSCALL_GETPPID:
		return 1;
	case IR0_SYSCALL_GETUID:
		return g_uid;
	case IR0_SYSCALL_GETEUID:
		return g_euid;
	case IR0_SYSCALL_GETGID:
		return g_gid;
	case IR0_SYSCALL_GETEGID:
		return g_egid;
	case IR0_SYSCALL_UNAME:
		return early_uname(context, a0);
	case IR0_SYSCALL_SET_ROBUST_LIST:
	case IR0_SYSCALL_RSEQ:
		return 0;
	case IR0_SYSCALL_PRCTL:
		return early_prctl(context, a0, a1, a2, a3, a4);
	case IR0_SYSCALL_PRLIMIT64:
		return early_prlimit64(context, a3);
	case IR0_SYSCALL_GETRANDOM:
		return early_getrandom(context, a0, a1);
	case IR0_SYSCALL_EXIT:
	case IR0_SYSCALL_EXIT_GROUP:
		return early_exit(context, a0);
	case IR0_SYSCALL_WAIT4:
		return early_wait4(context, (int64_t)a0, a1, a2);
	case IR0_SYSCALL_WAITID:
		return early_waitid(context, a0, a1, a2, a3);
	case IR0_SYSCALL_CLONE:
		return early_clone(context, a0, a1, a2, a3, a4, 0, 0);
	case IR0_SYSCALL_CLONE3:
		return early_clone3(context, a0, a1);
	case IR0_SYSCALL_VFORK:
		return early_clone(context,
				   CLONE_VM | CLONE_VFORK | IR0_EARLY_SIGCHLD,
				   0, 0, 0, 0, 0, 0);
	case IR0_SYSCALL_GETPGID:
		if (a0 == EARLY_CHILD_PID && early_child_exists())
			return 1;
		return ir0_early_process_getpgid(a0);
	case IR0_SYSCALL_GETSID:
		if (a0 == EARLY_CHILD_PID && early_child_exists())
			return 1;
		return ir0_early_process_getsid(a0);
	case IR0_SYSCALL_SETSID:
		return ir0_early_process_setsid();
	case IR0_SYSCALL_SETPGID:
		if (a0 == EARLY_CHILD_PID && early_child_exists())
			return -EPERM;
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
	case IR0_SYSCALL_RT_SIGSUSPEND:
		return -EINTR;
	case IR0_SYSCALL_KILL:
		if ((int64_t)a0 == EARLY_CHILD_PID && early_child_exists())
			return a1 > 64 ? -EINVAL : 0;
		if ((int64_t)a0 == -1 && early_child_exists())
			return a1 > 64 ? -EINVAL : 0;
		return ir0_early_process_kill((int64_t)a0, a1);
	case IR0_SYSCALL_TKILL:
		if ((int64_t)a0 == EARLY_CHILD_PID && early_child_exists())
			return a1 > 64 ? -EINVAL : 0;
		return ir0_early_process_tkill((int64_t)a0, a1);
	case IR0_SYSCALL_TGKILL:
		if ((int64_t)a1 == EARLY_CHILD_PID && early_child_exists())
			return a2 > 64 ? -EINVAL : 0;
		return ir0_early_process_tgkill((int64_t)a0, (int64_t)a1, a2);
	case IR0_SYSCALL_GETRESUID:
		return early_getresids(context, a0, a1, a2, 0);
	case IR0_SYSCALL_GETRESGID:
		return early_getresids(context, a0, a1, a2, 1);
	case IR0_SYSCALL_SETUID:
		if (g_euid == 0)
		{
			g_uid = g_euid = g_suid = g_fsuid = (uint32_t)a0;
			return 0;
		}
		if ((uint32_t)a0 == g_uid || (uint32_t)a0 == g_suid)
		{
			g_euid = g_fsuid = (uint32_t)a0;
			return 0;
		}
		return -EPERM;
	case IR0_SYSCALL_SETGID:
		if (g_egid == 0)
		{
			g_gid = g_egid = g_sgid = g_fsgid = (uint32_t)a0;
			return 0;
		}
		if ((uint32_t)a0 == g_gid || (uint32_t)a0 == g_sgid)
		{
			g_egid = g_fsgid = (uint32_t)a0;
			return 0;
		}
		return -EPERM;
	case IR0_SYSCALL_SETFSUID:
	{
		uint32_t prev = g_fsuid;

		if (g_euid == 0 || (uint32_t)a0 == g_uid ||
		    (uint32_t)a0 == g_euid || (uint32_t)a0 == g_suid)
			g_fsuid = (uint32_t)a0;
		return prev;
	}
	case IR0_SYSCALL_SETFSGID:
	{
		uint32_t prev = g_fsgid;

		if (g_egid == 0 || (uint32_t)a0 == g_gid ||
		    (uint32_t)a0 == g_egid || (uint32_t)a0 == g_sgid)
			g_fsgid = (uint32_t)a0;
		return prev;
	}
	case IR0_SYSCALL_SETREUID:
		if (a0 != UID_NOCHANGE)
		{
			if (g_euid != 0 && (uint32_t)a0 != g_uid &&
			    (uint32_t)a0 != g_suid)
				return -EPERM;
			g_uid = (uint32_t)a0;
		}
		if (a1 != UID_NOCHANGE)
		{
			if (g_euid != 0 && (uint32_t)a1 != g_uid &&
			    (uint32_t)a1 != g_suid)
				return -EPERM;
			g_euid = g_fsuid = (uint32_t)a1;
		}
		return 0;
	case IR0_SYSCALL_SETREGID:
		if (a0 != UID_NOCHANGE)
		{
			if (g_egid != 0 && (uint32_t)a0 != g_gid &&
			    (uint32_t)a0 != g_sgid)
				return -EPERM;
			g_gid = (uint32_t)a0;
		}
		if (a1 != UID_NOCHANGE)
		{
			if (g_egid != 0 && (uint32_t)a1 != g_gid &&
			    (uint32_t)a1 != g_sgid)
				return -EPERM;
			g_egid = g_fsgid = (uint32_t)a1;
		}
		return 0;
	case IR0_SYSCALL_SETRESUID:
		if (g_euid != 0)
		{
			if (a0 != UID_NOCHANGE && (uint32_t)a0 != g_uid &&
			    (uint32_t)a0 != g_euid && (uint32_t)a0 != g_suid)
				return -EPERM;
			if (a1 != UID_NOCHANGE && (uint32_t)a1 != g_uid &&
			    (uint32_t)a1 != g_euid && (uint32_t)a1 != g_suid)
				return -EPERM;
			if (a2 != UID_NOCHANGE && (uint32_t)a2 != g_uid &&
			    (uint32_t)a2 != g_euid && (uint32_t)a2 != g_suid)
				return -EPERM;
		}
		if (a0 != UID_NOCHANGE)
			g_uid = (uint32_t)a0;
		if (a1 != UID_NOCHANGE)
			g_euid = g_fsuid = (uint32_t)a1;
		if (a2 != UID_NOCHANGE)
			g_suid = (uint32_t)a2;
		return 0;
	case IR0_SYSCALL_SETRESGID:
		if (a0 != UID_NOCHANGE)
			g_gid = (uint32_t)a0;
		if (a1 != UID_NOCHANGE)
			g_egid = g_fsgid = (uint32_t)a1;
		if (a2 != UID_NOCHANGE)
			g_sgid = (uint32_t)a2;
		return 0;
	case IR0_SYSCALL_GETPRIORITY:
		if (a0 != PRIO_PROCESS)
			return -EINVAL;
		if (a1 != 0 && a1 != 1 &&
		    !(a1 == EARLY_CHILD_PID && early_child_exists()))
			return -ESRCH;
		return 20 + g_nice;
	case IR0_SYSCALL_SETPRIORITY:
		if (a0 != PRIO_PROCESS)
			return -EINVAL;
		if (a1 != 0 && a1 != 1 &&
		    !(a1 == EARLY_CHILD_PID && early_child_exists()))
			return -ESRCH;
		if ((int64_t)a2 < -20)
			g_nice = -20;
		else if ((int64_t)a2 > 19)
			g_nice = 19;
		else
			g_nice = (int32_t)a2;
		return 0;
	case IR0_SYSCALL_GET_ROBUST_LIST:
	{
		uint64_t head = 0;
		uint64_t len = 24;
		const struct early_user_services *user =
			context && context->services ? context->services->user : 0;

		if (a0 != 0 && a0 != 1 &&
		    !(a0 == EARLY_CHILD_PID && early_child_exists()))
			return -ESRCH;
		if (!user || !user->copy_to_user)
			return -ENOSYS;
		if (a1 && user->copy_to_user(a1, &head, sizeof(head)) != 0)
			return -EFAULT;
		if (a2 && user->copy_to_user(a2, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	case IR0_SYSCALL_SYSLOG:
		return a0 <= 10 ? 0 : -EINVAL;
	case IR0_SYSCALL_REBOOT:
		return early_reboot(context, a0, a1);
	case IR0_SYSCALL_EXECVEAT:
	{
		int64_t ret = early_rootfs_execve(a1, a2, a3);

		if (ret == 0)
		{
			early_process_note_exec();
			if (context && context->leave_usermode)
				*context->leave_usermode = 1;
		}
		return ret;
	}
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
		(void)early_process_request_yield();
		return 0;
	case IR0_SYSCALL_GETCPU:
		return early_getcpu(context, a0, a1);
	case IR0_SYSCALL_SCHED_SETPARAM:
		return early_sched_setparam(context, (int64_t)a0, a1);
	case IR0_SYSCALL_SCHED_GETPARAM:
		return early_sched_getparam(context, (int64_t)a0, a1);
	case IR0_SYSCALL_SCHED_SETSCHEDULER:
		return early_sched_setscheduler(context, (int64_t)a0, (int)a1, a2);
	case IR0_SYSCALL_SCHED_GETSCHEDULER:
		return early_sched_getscheduler((int64_t)a0);
	case IR0_SYSCALL_SCHED_SETAFFINITY:
		return early_sched_setaffinity(context, (int64_t)a0, a1, a2);
	case IR0_SYSCALL_SCHED_GETAFFINITY:
		return early_sched_getaffinity(context, (int64_t)a0, a1, a2);
	case IR0_SYSCALL_PROCESS_VM_READV:
		return early_process_vm(context, (int64_t)a0, a1, a2, a3, a4, a5, 0);
	case IR0_SYSCALL_PROCESS_VM_WRITEV:
		return early_process_vm(context, (int64_t)a0, a1, a2, a3, a4, a5, 1);
	case IR0_SYSCALL_PROCESS_MADVISE:
	{
		const struct early_user_services *user =
			context && context->services ? context->services->user : 0;
		int32_t pid;
		uint64_t i;
		int64_t total = 0;
		uint64_t vec[2];

		if (a4)
			return -EINVAL;
		if (a2 > IR0_EARLY_IOV_MAX)
			return -EINVAL;
		pid = early_rootfs_pidfd_pid((int)a0);
		if (pid < 0)
			return pid;
		if (!a2)
			return 0;
		if (!user || !user->copy_from_user)
			return -EFAULT;
		for (i = 0; i < a2; i++)
		{
			if (user->copy_from_user(vec,
						 a1 + i * IR0_EARLY_IOV_BYTES,
						 sizeof(vec)) != 0)
				return -EFAULT;
			if (!vec[1])
				continue;
			total += (int64_t)vec[1];
		}
		(void)a3;
		return total;
	}
	case IR0_SYSCALL_MEMBARRIER:
		return early_membarrier((int)a0, (int)a1);
	case IR0_SYSCALL_TIMES:
		return early_times(context, a0);
	case IR0_SYSCALL_SETHOSTNAME:
		return early_set_uts_field(context, a0, a1, g_nodename);
	case IR0_SYSCALL_SETDOMAINNAME:
		return early_set_uts_field(context, a0, a1, g_domainname);
	case IR0_SYSCALL_CAPGET:
		return early_capget(context, a0, a1);
	case IR0_SYSCALL_CAPSET:
		return early_capset(context, a0, a1);
	case IR0_SYSCALL_IOPRIO_SET:
		return early_ioprio_set((int)a0, (int64_t)a1, (int)a2);
	case IR0_SYSCALL_IOPRIO_GET:
		return early_ioprio_get((int)a0, (int64_t)a1);
	case IR0_SYSCALL_SCHED_GET_PRIORITY_MAX:
		return early_sched_priority_limit((int)a0, 1);
	case IR0_SYSCALL_SCHED_GET_PRIORITY_MIN:
		return early_sched_priority_limit((int)a0, 0);
	case IR0_SYSCALL_SCHED_RR_GET_INTERVAL:
		return early_sched_rr_interval(context, (int64_t)a0, a1);
	case IR0_SYSCALL_SCHED_GETATTR:
		return early_sched_getattr(context, (int64_t)a0, a1, a2);
	case IR0_SYSCALL_SCHED_SETATTR:
		return early_sched_setattr(context, (int64_t)a0, a1, a2);
	case IR0_SYSCALL_KCMP:
		return early_kcmp((int64_t)a0, (int64_t)a1, (int)a2, a3, a4);
	case IR0_SYSCALL_PIDFD_OPEN:
	{
		int64_t fd = early_pidfd_open((int64_t)a0, (unsigned)a1);

		if (fd >= 0 && context && context->services &&
		    context->services->time && context->services->time->note)
			context->services->time->note(EARLY_SYSCALL_PIDFD_OK);
		return fd;
	}
	case IR0_SYSCALL_PIDFD_SEND_SIGNAL:
		return early_pidfd_send_signal((int)a0, a1, a2, (unsigned)a3);
	case IR0_SYSCALL_PIDFD_GETFD:
		return early_pidfd_getfd((int)a0, (int)a1, (unsigned)a2);
	case IR0_SYSCALL_SIGALTSTACK:
		return early_sigaltstack(context, a0, a1);
	case IR0_SYSCALL_FUTEX:
		return early_futex(context, a0, a1, a2, a3);
	case IR0_SYSCALL_FUTEX_WAITV:
		return early_futex_waitv(context, a0, (unsigned)a1, (unsigned)a2,
					 a3, (int64_t)a4);
	case IR0_SYSCALL_FUTEX_WAKE:
		return early_futex_wake2(context, a0, a1, (int)a2, (unsigned)a3);
	case IR0_SYSCALL_FUTEX_WAIT:
		return early_futex_wait2(context, a0, a1, a2, (unsigned)a3, a4,
					 (int64_t)a5);
	case IR0_SYSCALL_FUTEX_REQUEUE:
		return early_futex_requeue(context, a0, (unsigned)a1, (int)a2,
					   (int)a3);
	case IR0_SYSCALL_LSM_GET_SELF_ATTR:
	case IR0_SYSCALL_LSM_SET_SELF_ATTR:
		if (a3 & ~(uint64_t)IR0_EARLY_LSM_FLAG_SINGLE)
			return -EINVAL;
		return -EOPNOTSUPP;
	case IR0_SYSCALL_LSM_LIST_MODULES:
	{
		const struct early_user_services *user =
			context && context->services ? context->services->user : 0;
		uint32_t need = 8;
		uint32_t have = 0;
		uint64_t id = IR0_EARLY_LSM_ID_LANDLOCK;

		if (a2)
			return -EINVAL;
		if (!a1 || !user || !user->copy_from_user || !user->copy_to_user)
			return -EFAULT;
		if (user->copy_from_user(&have, a1, 4) != 0)
			return -EFAULT;
		if (have < need)
		{
			if (user->copy_to_user(a1, &need, 4) != 0)
				return -EFAULT;
			return -E2BIG;
		}
		if (!a0 || user->copy_to_user(a0, &id, 8) != 0)
			return -EFAULT;
		if (user->copy_to_user(a1, &need, 4) != 0)
			return -EFAULT;
		return 1;
	}
	case IR0_SYSCALL_EXECVE:
	{
		int64_t ret = early_rootfs_execve(a0, a1, a2);

		if (ret == 0)
		{
			early_process_note_exec();
			if (context && context->leave_usermode)
				*context->leave_usermode = 1;
		}
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
	IR0_SYSCALL_LSM_LIST_MODULES,
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
	.handler = early_process_syscall,
};

int early_process_clone_take_enter(uint64_t *stack, uint64_t *tls)
{
	if (!g_enter_child)
		return 0;
	g_enter_child = 0;
	g_current_pid = EARLY_CHILD_PID;
	g_child_state = CHILD_RUNNING;
	if (stack)
		*stack = g_child_stack;
	if (tls)
		*tls = g_child_tls;
	return 1;
}

void early_process_clone_save_parent(const uint64_t *frame)
{
	unsigned int i;

	if (!frame)
		return;
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		g_parent_frame[i] = frame[i];
}

int early_process_clone_resume_parent(uint64_t *frame)
{
	unsigned int i;

	if (!g_resume_parent || !frame)
		return 0;
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		frame[i] = g_parent_frame[i];
	g_resume_parent = 0;
	g_current_pid = 1;
	return 1;
}

int early_process_is_root(void)
{
	return g_euid == 0;
}

void early_process_note_exec(void)
{
	if (g_current_pid == EARLY_CHILD_PID && g_vfork)
	{
		g_vfork = 0;
		g_vfork_exec = 1;
		g_resume_parent = 1;
	}
}

int early_process_vfork_exec_resume(uint64_t *frame)
{
	unsigned int i;

	if (!frame || !g_vfork_exec || !g_resume_parent ||
	    g_current_pid != EARLY_CHILD_PID)
		return 0;
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		g_child_frame[i] = frame[i];
	g_child_frame_valid = 1;
	g_child_state = CHILD_PARKED;
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		frame[i] = g_parent_frame[i];
	g_vfork_exec = 0;
	g_resume_parent = 0;
	g_current_pid = 1;
	return 1;
}

int early_process_request_yield(void)
{
	if (g_current_pid == EARLY_CHILD_PID)
	{
		if (g_vfork || g_child_state == CHILD_ZOMBIE)
			return 0;
		g_yield_requested = 1;
		return 1;
	}
	if (g_child_state == CHILD_RUNNING || g_child_state == CHILD_PARKED)
	{
		g_yield_requested = 1;
		return 1;
	}
	return 0;
}

int early_process_take_yield(uint64_t *frame)
{
	uint64_t *save;
	uint64_t *load;
	unsigned int i;
	int dest;
	int rewind;

	if (!frame || g_enter_child || g_resume_parent)
		return 0;
	dest = 0;
	rewind = 0;
	if (g_yield_requested)
	{
		if (g_current_pid == EARLY_CHILD_PID && g_vfork)
		{
			g_yield_requested = 0;
			return 0;
		}
		dest = (g_current_pid == 1) ? EARLY_CHILD_PID : 1;
		rewind = 1;
		g_yield_requested = 0;
	}
	else if (g_current_pid == 1 && g_child_state == CHILD_PARKED &&
		 g_child_frame_valid)
		dest = EARLY_CHILD_PID;
	if (!dest)
		return 0;
	if (dest == EARLY_CHILD_PID && !g_child_frame_valid &&
	    g_current_pid == 1)
		return 0;
	save = (g_current_pid == 1) ? g_parent_frame : g_child_frame;
	load = (dest == 1) ? g_parent_frame : g_child_frame;
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		save[i] = frame[i];
	if (rewind && save[31] >= IR0_EARLY_SVC_INSN_BYTES)
		save[31] -= IR0_EARLY_SVC_INSN_BYTES;
	if (g_current_pid == EARLY_CHILD_PID)
	{
		g_child_frame_valid = 1;
		g_child_state = CHILD_PARKED;
	}
	for (i = 0; i < IR0_EARLY_EL0_FRAME_WORDS; i++)
		frame[i] = load[i];
	if (dest == EARLY_CHILD_PID && g_child_state == CHILD_PARKED)
	{
		g_child_state = CHILD_RUNNING;
		g_block_wait_ok = 1;
	}
	g_current_pid = dest;
	return 1;
}

int early_process_block_wait_observed(void)
{
	return g_block_wait_ok;
}

int64_t early_process_current_pid(void)
{
	return g_current_pid;
}

void early_process_current_ucred(int32_t *pid, int32_t *uid, int32_t *gid)
{
	if (pid)
		*pid = (int32_t)g_current_pid;
	if (uid)
		*uid = (int32_t)g_uid;
	if (gid)
		*gid = (int32_t)g_gid;
}

int early_process_smoke_ok(void)
{
	return g_getpid_ok;
}
