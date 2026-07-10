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
#include "syscalls_glue.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/scheduler_api.h>
#include <ir0/permissions.h>
#include <ir0/clock_wait.h>
#include <ir0/clock.h>
#include <ir0/debug_runtime.h>
#include <ir0/serial_io.h>
#include <string.h>

#include <ir0/oops.h>
#include <ir0/kernel.h>
#include <ir0/elf_loader.h>
#include <ir0/path.h>
#include <ir0/permissions.h>
#include <ir0/fase50_debug.h>
#include <ir0/fase51_debug.h>
#include <ir0/fase52_debug.h>
#include <ir0/serial_io.h>
#include <ir0/kmem.h>
#include <ir0/validation.h>
#include <ir0/debug_runtime.h>
#include <ir0/scheduler_api.h>
#include <ktm_probe_diag.h>
#include <config.h>
#include <ir0/arch_port.h>
#include <ir0/time.h>
#include <ir0/clock.h>
#include <ir0/credentials.h>
#include <ir0/power_manag.h>

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

#define FUTEX_WAIT 0
#define FUTEX_WAKE 1


#define IR0_ROBUST_LIST_SIZE 24

void process_cred_init_groups(process_t *p)
{
	if (!p)
		return;
	if (p->ngroups == 0)
	{
		p->groups[0] = (gid_t)p->gid;
		p->ngroups = 1;
	}
}

int process_cred_in_group(const process_t *p, gid_t gid)
{
	uint8_t i;

	if (!p)
		return 0;
	if ((gid_t)p->egid == gid)
		return 1;
	for (i = 0; i < p->ngroups; i++)
	{
		if (p->groups[i] == gid)
			return 1;
	}
	return 0;
}

void process_reset_signals_on_exec(process_t *p)
{
	signals_reset_on_exec(p);
}

int64_t sys_getgroups(int size, gid_t *list)
{
	uint8_t n;
	int i;

	if (!current_process)
		return -ESRCH;

	process_cred_init_groups(current_process);
	n = current_process->ngroups;

	if (size == 0)
		return (int64_t)n;

	if (!list || size < 0)
		return -EINVAL;
	if (validate_userspace_buffer(list, (size_t)size * sizeof(gid_t)) != 0)
		return -EFAULT;
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
	size_t i;

	if (!current_process)
		return -ESRCH;
	if (current_process->euid != ROOT_UID)
		return -EPERM;
	if (size > IR0_NGROUPS_MAX)
		return -EINVAL;
	if (size > 0 && !list)
		return -EFAULT;
	if (size > 0 &&
	    validate_userspace_buffer(list, size * sizeof(gid_t)) != 0)
		return -EFAULT;

	for (i = 0; i < size; i++)
	{
		gid_t g;
		if (copy_from_user(&g, &list[i], sizeof(g)) != 0)
			return -EFAULT;
		current_process->groups[i] = g;
	}
	current_process->ngroups = (uint8_t)size;
	if (size == 0)
	{
		current_process->groups[0] = (gid_t)current_process->gid;
		current_process->ngroups = 1;
	}
	return 0;
}

int64_t sys_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
	(void)suid;

	if (!current_process)
		return -ESRCH;

	if (current_process->euid == ROOT_UID)
	{
		if ((int)ruid != (int)-1)
		{
			current_process->uid = (uint32_t)ruid;
			current_process->euid = (uint32_t)ruid;
		}
		if ((int)euid != (int)-1)
			current_process->euid = (uint32_t)euid;
		return 0;
	}

	if ((int)euid != (int)-1 &&
	    (uint32_t)euid != current_process->uid &&
	    (uint32_t)euid != current_process->euid)
		return -EPERM;
	if ((int)ruid != (int)-1 &&
	    (uint32_t)ruid != current_process->uid &&
	    (uint32_t)ruid != current_process->euid)
		return -EPERM;

	if ((int)ruid != (int)-1)
		current_process->uid = (uint32_t)ruid;
	if ((int)euid != (int)-1)
		current_process->euid = (uint32_t)euid;
	return 0;
}

int64_t sys_getresuid(uid_t *ruid, uid_t *euid, uid_t *suid)
{
	uid_t u;
	uid_t eu;

	if (!current_process)
		return -ESRCH;

	u = (uid_t)current_process->uid;
	eu = (uid_t)current_process->euid;

	if (ruid)
	{
		if (validate_userspace_buffer(ruid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(ruid, &u, sizeof(u)) != 0)
			return -EFAULT;
	}
	if (euid)
	{
		if (validate_userspace_buffer(euid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(euid, &eu, sizeof(eu)) != 0)
			return -EFAULT;
	}
	if (suid)
	{
		if (validate_userspace_buffer(suid, sizeof(uid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(suid, &u, sizeof(u)) != 0)
			return -EFAULT;
	}
	return 0;
}

int64_t sys_setresgid(gid_t rgid, gid_t egid, gid_t sgid)
{
	(void)sgid;

	if (!current_process)
		return -ESRCH;

	if (current_process->euid == ROOT_UID)
	{
		if ((int)rgid != (int)-1)
		{
			current_process->gid = (uint32_t)rgid;
			current_process->egid = (uint32_t)rgid;
		}
		if ((int)egid != (int)-1)
			current_process->egid = (uint32_t)egid;
		process_cred_init_groups(current_process);
		current_process->groups[0] = (gid_t)current_process->gid;
		current_process->ngroups = 1;
		return 0;
	}

	if ((int)egid != (int)-1 &&
	    (uint32_t)egid != current_process->gid &&
	    (uint32_t)egid != current_process->egid)
		return -EPERM;
	if ((int)rgid != (int)-1 &&
	    (uint32_t)rgid != current_process->gid &&
	    (uint32_t)rgid != current_process->egid)
		return -EPERM;

	if ((int)rgid != (int)-1)
		current_process->gid = (uint32_t)rgid;
	if ((int)egid != (int)-1)
		current_process->egid = (uint32_t)egid;
	process_cred_init_groups(current_process);
	current_process->groups[0] = (gid_t)current_process->gid;
	current_process->ngroups = 1;
	return 0;
}

int64_t sys_getresgid(gid_t *rgid, gid_t *egid, gid_t *sgid)
{
	gid_t g;
	gid_t eg;

	if (!current_process)
		return -ESRCH;

	g = (gid_t)current_process->gid;
	eg = (gid_t)current_process->egid;

	if (rgid)
	{
		if (validate_userspace_buffer(rgid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(rgid, &g, sizeof(g)) != 0)
			return -EFAULT;
	}
	if (egid)
	{
		if (validate_userspace_buffer(egid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(egid, &eg, sizeof(eg)) != 0)
			return -EFAULT;
	}
	if (sgid)
	{
		if (validate_userspace_buffer(sgid, sizeof(gid_t)) != 0)
			return -EFAULT;
		if (copy_to_user(sgid, &g, sizeof(g)) != 0)
			return -EFAULT;
	}
	return 0;
}

static uint32_t rt_sigaction_mask_from_sigset(const sigset_t *set, size_t sigsetsize)
{
	uint64_t legacy64;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (!set)
			return 0;
		memcpy(&legacy64, set, sizeof(legacy64));
		return (uint32_t)legacy64;
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
		legacy64 = (uint64_t)mask;
		memcpy(set, &legacy64, sizeof(legacy64));
		return;
	}
	ir0_sigset_set_low32(set, mask);
}

/*
 * Linux/musl may pass sigsetsize=8; user buffers are sized for the uapi layout,
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
		if (validate_userspace_buffer(oldact, user_bytes) != 0)
			return -EFAULT;
		memset(&kold, 0, sizeof(kold));
		kold.sa_handler = current_process->signal_handlers[signum];
		rt_sigaction_mask_to_sigset(&kold.sa_mask, sigsetsize,
					    current_process->signal_sa_mask[signum]);
		kold.sa_flags = (unsigned long)current_process->signal_sa_flags[signum];
		if (copy_to_user(oldact, &kold, user_bytes) != 0)
			return -EFAULT;
	}

	if (act)
	{
		if (validate_userspace_buffer(act, user_bytes) != 0)
			return -EFAULT;
		memset(&kact, 0, sizeof(kact));
		if (copy_from_user(&kact, act, user_bytes) != 0)
			return -EFAULT;
		if (register_signal_handler(signum, kact.sa_handler) != 0)
			return -EFAULT;
		current_process->signal_sa_mask[signum] =
			rt_sigaction_mask_from_sigset(&kact.sa_mask, sigsetsize);
		current_process->signal_sa_flags[signum] = (uint32_t)kact.sa_flags;
#if defined(SIGNAL_DELIVER_LOG) && SIGNAL_DELIVER_LOG
		if (signum == SIGSEGV)
		{
			serial_print("[SIGNAL][REG] pid=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" sig=SIGSEGV handler=");
			serial_print_hex64((uint64_t)(uintptr_t)kact.sa_handler);
			serial_print(" flags=");
			serial_print_hex32((uint32_t)kact.sa_flags);
			serial_print(" proc_mask=");
			serial_print_hex32(current_process->signal_mask);
			serial_print("\n");
		}
#endif
	}

	return 0;
}

int64_t sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset,
			   size_t sigsetsize)
{
	sigset_t kset;
	uint32_t newmask;
	uint64_t legacy64;

	if (!current_process)
		return -ESRCH;
	if (sigsetsize != sizeof(sigset_t) && sigsetsize != sizeof(uint64_t))
		return -EINVAL;

	if (oldset)
	{
		sigset_t kold;

		if (sigsetsize == sizeof(uint64_t))
		{
			if (validate_userspace_buffer(oldset, sizeof(uint64_t)) != 0)
				return -EFAULT;
			legacy64 = (uint64_t)current_process->signal_mask;
			if (copy_to_user(oldset, &legacy64, sizeof(legacy64)) != 0)
				return -EFAULT;
		}
		else
		{
			if (validate_userspace_buffer(oldset, sizeof(sigset_t)) != 0)
				return -EFAULT;
			ir0_sigset_set_low32(&kold, current_process->signal_mask);
			if (copy_to_user(oldset, &kold, sizeof(kold)) != 0)
				return -EFAULT;
		}
	}

	if (!set)
		return 0;

	if (sigsetsize == sizeof(uint64_t))
	{
		if (validate_userspace_buffer(set, sizeof(uint64_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&legacy64, set, sizeof(legacy64)) != 0)
			return -EFAULT;
		newmask = (uint32_t)legacy64;
	}
	else
	{
		if (validate_userspace_buffer(set, sizeof(sigset_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&kset, set, sizeof(kset)) != 0)
			return -EFAULT;
		newmask = ir0_sigset_low32(&kset);
	}
	switch (how)
	{
	case SIG_BLOCK:
		current_process->signal_mask |= newmask;
		break;
	case SIG_UNBLOCK:
		current_process->signal_mask &= ~newmask;
		break;
	case SIG_SETMASK:
		current_process->signal_mask = newmask;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/*
 * sys_rt_sigsuspend - Linux rt_sigsuspend(2): replace mask and wait for signal.
 * Always returns -EINTR when a non-blocked signal is pending (musl sigsuspend).
 */
int64_t sys_rt_sigsuspend(const sigset_t *mask, size_t sigsetsize)
{
	sigset_t kset;
	uint32_t saved_mask;

	if (!current_process)
		return -ESRCH;
	if (!mask || sigsetsize != sizeof(sigset_t))
		return -EINVAL;
	if (validate_userspace_buffer((void *)mask, sizeof(sigset_t)) != 0)
		return -EFAULT;
	if (copy_from_user(&kset, mask, sizeof(kset)) != 0)
		return -EFAULT;

	saved_mask = current_process->signal_mask;
	current_process->signal_mask = ir0_sigset_low32(&kset);

	for (;;)
	{
		if (current_process->signal_pending &
		    ~current_process->signal_mask)
			break;

		current_process->state = PROCESS_BLOCKED;
		process_arm_kernel_syscall_sleep(current_process);
		while (current_process->state == PROCESS_BLOCKED)
		{
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
	}

	current_process->signal_mask = saved_mask;
	return -EINTR;
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

struct robust_list_head
{
	void *list;
	void *list_op_pending;
	void *list_op_next;
};

int64_t sys_set_robust_list(struct robust_list_head *head, size_t len)
{
	if (!current_process)
		return -ESRCH;

	if (len != sizeof(struct robust_list_head))
		return -EINVAL;

	if (head && current_process->mode == USER_MODE)
	{
		if (validate_userspace_buffer(head, sizeof(struct robust_list_head)) != 0)
			return -EFAULT;
	}

	current_process->robust_list = head;
	return 0;
}

int64_t sys_setsid(void)
{
	if (!current_process)
		return -ESRCH;
	return (int64_t)current_process->task.pid;
}

static uint64_t fase50_count_open_fds_local(process_t *p)
{
#if CONFIG_DEBUG_FASE50
	uint64_t n = 0;
	int i;

	if (!p)
		return 0;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (p->fd_table[i].in_use)
			n++;
	}
	return n;
#else
	(void)p;
	return 0;
#endif
}
static void fase50_trace_syscall_proc(const char *stage, process_t *p)
{
#if CONFIG_DEBUG_FASE50
	serial_print("[FASE50][TRACE] stage=");
	serial_print(stage ? stage : "(null)");
	serial_print(" current=");
	serial_print_hex64((uint64_t)(uintptr_t)current_process);
	serial_print(" proc=");
	serial_print_hex64((uint64_t)(uintptr_t)p);
	serial_print(" pid=");
	serial_print_hex32(p ? (uint32_t)p->task.pid : 0);
	serial_print(" state=");
	serial_print_hex64((uint64_t)(p ? p->state : 0));
	serial_print(" mm=");
	serial_print_hex64(p ? (uint64_t)(uintptr_t)p->page_directory : 0);
	serial_print(" files=");
	serial_print_hex64(p ? (uint64_t)(uintptr_t)p->fd_table : 0);
	serial_print(" fds_open=");
	serial_print_hex64(fase50_count_open_fds_local(p));
	serial_print(" task_cr3=");
	serial_print_hex64(p ? p->task.cr3 : 0);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");
#else
	(void)stage;
	(void)p;
#endif
}
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
	case LINUX_REBOOT_CMD_CAD_OFF:
		/* CAD not wired yet; accept as no-op like a stub capability. */
		return 0;
	case LINUX_REBOOT_CMD_RESTART:
	case LINUX_REBOOT_CMD_RESTART2:
		kernel_system_shutdown(IR0_SYSTEM_REBOOT);
		break;
	case LINUX_REBOOT_CMD_HALT:
		kernel_system_shutdown(IR0_SYSTEM_HALT);
		break;
	case LINUX_REBOOT_CMD_POWER_OFF:
		kernel_system_shutdown(IR0_SYSTEM_POWEROFF);
		break;
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

  serial_print("GETTID pid=");
  serial_print_hex32((uint32_t)tid);
  serial_print("\n");

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

  if (current_process->euid == ROOT_UID)
  {
    current_process->uid = (uint32_t)uid;
    current_process->euid = (uint32_t)uid;
    return 0;
  }

  if ((uint32_t)uid == current_process->uid || (uint32_t)uid == current_process->euid)
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
    return 0;
  }

  if ((uint32_t)gid == current_process->gid || (uint32_t)gid == current_process->egid)
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

int64_t sys_sudo_auth(const char *password)
{
  if (!current_process || !password)
    return -EFAULT;
  if (validate_userspace_string(password, 64) != 0)
    return -EFAULT;

  if (current_process->euid == ROOT_UID)
    return 0;

  if (!user_exists(current_process->uid))
    return -EPERM;
  if (auth_user_password(current_process->uid, password) != 0)
    return -EACCES;

  current_process->euid = ROOT_UID;
  current_process->egid = ROOT_GID;
  return 0;
}
int64_t sys_exec(const char *pathname,
                 char *const argv[],
                 char *const envp[])
{
  int exec_argv_slots_seen = 0;
  serial_print("SERIAL: sys_exec called\n");
  fase50_trace_syscall_proc("sys_exec-entry", current_process);

  if (!current_process || !pathname)
  {
    return -EFAULT;
  }

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Unix-style: resolve relative paths against cwd before loading */
  char resolved_path[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved_path, sizeof(resolved_path)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved_path;
  }
  else
  {
    if (normalize_path(pathname, resolved_path, sizeof(resolved_path)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved_path;
  }

  /* argv/envp are NULL-terminated vectors; only validate the first slot here.
   * Per-entry mapped checks happen inside the iteration below.
   */
  if (argv && !is_user_address(argv, sizeof(char *)))
    return -EFAULT;
  if (envp && !is_user_address(envp, sizeof(char *)))
    return -EFAULT;

  /* Copy argv and envp from userspace if provided */
  char *kernel_argv[256] = {NULL};
  char *kernel_envp[256] = {NULL};
  int argc_kept = 0;
  int envc_kept = 0;
  int rc_vec = 0;

  if (argv)
  {
    int i;
    for (i = 0; i < 256; i++)
    {
      const void *slot = (const char *)argv + i * sizeof(char *);
      char *user_ptr = NULL;

      /* Each slot is 8 bytes — within a single page; check mapping for that page. */
      if (!is_user_address_checked(slot, sizeof(char *), 1))
      {
        rc_vec = -EFAULT;
        break;
      }
      if (copy_from_user(&user_ptr, slot, sizeof(char *)) != 0)
      {
        rc_vec = -EFAULT;
        break;
      }
      if (user_ptr == NULL)
        break;  /* end of vector */
      exec_argv_slots_seen = i + 1;

      /* Validate string start is mapped (first byte covers its starting page). */
      if (!is_user_address_checked(user_ptr, 1, 1))
      {
        rc_vec = -EFAULT;
        break;
      }

      char *arg_str = (char *)kmalloc_try(256);
      if (!arg_str)
      {
        rc_vec = -ENOMEM;
        break;
      }
      if (copy_from_user(arg_str, user_ptr, 256) != 0)
      {
        kfree(arg_str);
        rc_vec = -EFAULT;
        break;
      }
      arg_str[255] = '\0';  /* force termination in case the user string was 256+ */
      kernel_argv[i] = arg_str;
      argc_kept = i + 1;
    }
    if (rc_vec)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      return rc_vec;
    }
    if (i == 256)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      return -E2BIG;
    }
  }

  if (DEBUG_FASE50)
  {
    serial_print("[FASE50][EXEC_ARGV] stage=sys_exec-captured pid=");
    serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
    serial_print(" path=");
    serial_print(path_to_use ? path_to_use : "(null)");
    serial_print(" argc=");
    serial_print_hex64((uint64_t)argc_kept);
    serial_print(" argv_slots_seen=");
    serial_print_hex64((uint64_t)exec_argv_slots_seen);
    serial_print("\n");
    for (int i = 0; i < argc_kept && i < 8; i++)
    {
      serial_print("[FASE50][EXEC_ARGV] stage=sys_exec-argv idx=");
      serial_print_hex32((uint32_t)i);
      serial_print(" str=");
      serial_print(kernel_argv[i] ? kernel_argv[i] : "(null)");
      serial_print("\n");
    }
  }

  fase51_dbg_exec_argv(path_to_use,
                       argc_kept > 0 ? kernel_argv[0] : NULL,
                       argc_kept > 1 ? kernel_argv[1] : NULL);
  fase52_dbg_exec_argv(path_to_use,
                       argc_kept > 0 ? kernel_argv[0] : NULL,
                       argc_kept > 1 ? kernel_argv[1] : NULL,
                       envp ? 1ULL : 0ULL);

  if (envp)
  {
    int i;
    for (i = 0; i < 256; i++)
    {
      const void *slot = (const char *)envp + i * sizeof(char *);
      char *user_ptr = NULL;

      if (!is_user_address_checked(slot, sizeof(char *), 1))
      {
        rc_vec = -EFAULT;
        break;
      }
      if (copy_from_user(&user_ptr, slot, sizeof(char *)) != 0)
      {
        rc_vec = -EFAULT;
        break;
      }
      if (user_ptr == NULL)
        break;

      if (!is_user_address_checked(user_ptr, 1, 1))
      {
        rc_vec = -EFAULT;
        break;
      }

      char *env_str = (char *)kmalloc_try(256);
      if (!env_str)
      {
        rc_vec = -ENOMEM;
        break;
      }
      if (copy_from_user(env_str, user_ptr, 256) != 0)
      {
        kfree(env_str);
        rc_vec = -EFAULT;
        break;
      }
      env_str[255] = '\0';
      kernel_envp[i] = env_str;
      envc_kept = i + 1;
    }
    if (rc_vec)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      for (int j = 0; j < envc_kept; j++)
        kfree(kernel_envp[j]);
      return rc_vec;
    }
    if (i == 256)
    {
      for (int j = 0; j < argc_kept; j++)
        kfree(kernel_argv[j]);
      for (int j = 0; j < envc_kept; j++)
        kfree(kernel_envp[j]);
      return -E2BIG;
    }
  }

  int64_t result;

  {
    char user_path_copy[256];

    user_path_copy[0] = '\0';
    if (copy_from_user(user_path_copy, pathname, sizeof(user_path_copy) - 1) == 0)
      user_path_copy[sizeof(user_path_copy) - 1] = '\0';

    if (DEBUG_FASE50)
    {
      serial_print("[EXEC_AUDIT][SYSCALL] pid=");
      serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
      serial_print(" user_path=");
      serial_print(user_path_copy[0] ? user_path_copy : "(copy_fail)");
      serial_print(" resolved_path=");
      serial_print(path_to_use ? path_to_use : "(null)");
      serial_print(" argv=");
      for (int ai = 0; ai < 4; ai++)
      {
        if (ai > 0)
          serial_print(",");
        if (ai < argc_kept && kernel_argv[ai])
          serial_print(kernel_argv[ai]);
        else
          serial_print("-");
      }
      serial_print("\n");
    }
  }

  if (current_process->mode == USER_MODE)
  {
    ktm_probe_diag_execve(current_process, path_to_use);
    fase50_trace_syscall_proc("sys_exec-before-exec_replace_current", current_process);
    result = exec_replace_current(path_to_use,
                                  argv ? (char *const *)kernel_argv : NULL,
                                  envp ? (char *const *)kernel_envp : NULL);
  }
  else
  {
    result = kexecve(path_to_use,
                     argv ? (char *const *)kernel_argv : NULL,
                     envp ? (char *const *)kernel_envp : NULL);
  }

  /* Clean up copied strings */
  for (int i = 0; i < 256 && kernel_argv[i]; i++)
    kfree(kernel_argv[i]);
  for (int i = 0; i < 256 && kernel_envp[i]; i++)
    kfree(kernel_envp[i]);

  fase50_trace_syscall_proc("sys_exec-return", current_process);
  return result;
}
int64_t sys_fork(void)
{
  int64_t r;
  if (!current_process)
    return -ESRCH;

  r = fork();
  return r;
}

/*
 * sys_clone — Linux __NR_clone (56).
 * CLONE_THREAD|CLONE_VM: lightweight thread sharing the caller's mm.
 * Otherwise duplicates the address space like fork().
 */
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
                  int *child_tid, unsigned long tls)
{
  if (!current_process)
    return -ESRCH;

  if (flags & 0x00010000UL) /* CLONE_THREAD */
    return (int64_t)clone_thread(flags, stack, parent_tid, child_tid, tls);

  (void)stack;
  (void)parent_tid;
  (void)child_tid;
  (void)tls;
  return fork();
}


int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  if (current_process->mode == USER_MODE)
  {
    pid_t wait_pid = pid;
    int wait_opts = options;

    /*
     * Drop stale pipe/poll irq_frame resume state and read wait args from the
     * pt_regs snapshot (captured before any nested schedule on the shared
     * syscall stack can clobber the C ABI parameter slots).
     */
    process_clear_in_thread_syscall_block(current_process);
    wait_pid = (pid_t)current_process->syscall_frame.rdi;
    wait_opts = (int)current_process->syscall_frame.rdx;
    current_process->wait_options = wait_opts;
    pid = wait_pid;
    options = wait_opts;
  }

  if (IR0_DEBUG_WAIT)
  {
    serial_print("[WAIT4_WNOHANG_AUDIT] wait_begin parent=");
    serial_print_hex32((uint32_t)current_process->task.pid);
    serial_print(" target=");
    serial_print_hex32((uint32_t)pid);
    serial_print(" options=");
    serial_print_hex32((uint32_t)options);
    serial_print(" wait_target_pid=");
    serial_print_hex32((uint32_t)current_process->wait_target_pid);
    serial_print(" wait_options=");
    serial_print_hex32((uint32_t)current_process->wait_options);
    serial_print(" wait_resume_child_pid=");
    serial_print_hex32((uint32_t)current_process->wait_resume_child_pid);
    serial_print(" syscall_resume_rax=");
    serial_print_hex64(current_process->syscall_resume_rax);
    serial_print("\n");
    serial_print("[WAIT_EXIT_AUDIT][sys_wait4] entry parent_pid=");
    serial_print_hex32((uint32_t)current_process->task.pid);
    serial_print(" wait_pid=");
    serial_print_hex32((uint32_t)pid);
    serial_print(" status_ptr=");
    serial_print_hex64((uint64_t)(uintptr_t)status);
    serial_print(" options=");
    serial_print_hex64((uint64_t)(unsigned int)options);
    serial_print("\n");
  }

  fase50_trace_syscall_proc("sys_wait4-entry", current_process);
#if IR0_DEBUG_PROC
  process_fase46_note_wait(current_process);
#endif
  {
    int64_t ret = process_wait(pid, status, options);

    if (current_process->state != PROCESS_BLOCKED)
      process_reset_blocked_syscall_state(current_process);

    if (IR0_DEBUG_WAIT)
    {
      serial_print("[WAIT4_WNOHANG_AUDIT] wait_return parent=");
      serial_print_hex32((uint32_t)current_process->task.pid);
      serial_print(" ret=");
      serial_print_hex64((uint64_t)ret);
      serial_print(" wait_resume_child_pid=");
      serial_print_hex32((uint32_t)current_process->wait_resume_child_pid);
      serial_print(" syscall_resume_rax=");
      serial_print_hex64(current_process->syscall_resume_rax);
      serial_print(" status_write=");
      serial_print(ret > 0 ? "yes" : "no");
      serial_print("\n");
      serial_print("[WAIT_EXIT_AUDIT][sys_wait4] return parent_pid=");
      serial_print_hex32((uint32_t)current_process->task.pid);
      serial_print(" ret=");
      serial_print_hex64((uint64_t)ret);
      serial_print("\n");
    }
    fase50_trace_syscall_proc("sys_wait4-return", current_process);
    fase51_dbg_wait4((int64_t)pid, ret);
    return ret;
  }
}

int64_t sys_waitpid(pid_t pid, int *status, int options)
{
  return sys_wait4(pid, status, options, NULL);
}

/**
 * sys_kill - Send a signal to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int64_t sys_kill(pid_t pid, int signal)
{
  if (!current_process)
    return -ESRCH;

  /* Validate signal number */
  if (signal < 0 || signal >= _NSIG)
    return -EINVAL;

  /* Can't send signal to PID 0 or negative */
  if (pid <= 0)
    return -EINVAL;

  /* Send signal to target process */
  if (send_signal(pid, signal) != 0)
    return -ESRCH; /* Process not found */

  if (signal != 0)
  {
    process_t *target = process_find_by_pid(pid);

    if (target)
      (void)process_signal_default_kill(target, signal);

    clock_request_sched_resched();
  }

#if IR0_DEBUG_PROC
  {
    process_t *target = process_find_by_pid(pid);

    if (target)
    {
      serial_print("[SIGTERM_AUDIT] sys_kill pid=");
      serial_print_hex32((uint32_t)pid);
      serial_print(" sig=");
      serial_print_hex32((uint32_t)signal);
      serial_print(" pending=");
      serial_print_hex32(target->signal_pending);
      serial_print(" state=");
      serial_print_hex32((uint32_t)target->state);
      serial_print("\n");
    }
  }
#endif

  return 0;
}

/**
 * sys_sigaction - Set signal action (POSIX sigaction)
 * @signum: Signal number
 * @act: New signal action (can be NULL)
 * @oldact: Old signal action (can be NULL, output)
 *
 * Returns: 0 on success, -1 on error
 */
int64_t sys_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact)
{
  if (!current_process)
    return -ESRCH;

  /* Validate signal number */
  if (signum < 1 || signum >= _NSIG)
    return -EINVAL;

  /* Signals that cannot be caught */
  if (signum == SIGKILL || signum == SIGSTOP)
    return -EINVAL;

  /* Save old action if requested */
  if (oldact)
  {
    /* Validate oldact is in userspace (for USER_MODE processes) */
    if (validate_userspace_buffer(oldact, sizeof(struct sigaction)) != 0)
      return -EFAULT;

    oldact->sa_handler = current_process->signal_handlers[signum];
    ir0_sigset_set_low32(&oldact->sa_mask, current_process->signal_mask);
    oldact->sa_flags = 0;
    oldact->sa_restorer = NULL;
  }

  /* Set new action if provided */
  if (act)
  {
    /* Validate act is in userspace (for USER_MODE processes) */
    if (validate_userspace_buffer((const void *)act, sizeof(struct sigaction)) != 0)
      return -EFAULT;

    /* Register new handler */
    if (register_signal_handler(signum, act->sa_handler) != 0)
      return -EFAULT;

    current_process->signal_sa_mask[signum] = ir0_sigset_low32(&act->sa_mask);
  }

  return 0;
}
int64_t sys_arch_prctl(int code, unsigned long addr)
{
  uint64_t fsbase;

  if (!current_process)
    return -ESRCH;

  if (code == ARCH_SET_FS)
  {
    current_process->fs_base = (uint64_t)addr;
    arch_set_fs_base((uint64_t)addr);
    return 0;
  }

  if (code == ARCH_GET_FS)
  {
    if (addr == 0)
      return -EINVAL;
    if (validate_userspace_buffer((void *)(uintptr_t)addr, sizeof(uint64_t)) != 0)
      return -EFAULT;
    fsbase = current_process->fs_base;
    if (copy_to_user((void *)(uintptr_t)addr, &fsbase, sizeof(uint64_t)) != 0)
      return -EFAULT;
    return 0;
  }

  return -EINVAL;
}

/*
 * sys_set_tid_address - Linux set_tid_address(2) stub for musl.
 */
int64_t sys_set_tid_address(int *tidptr)
{
  if (!current_process)
    return -ESRCH;

  if (tidptr && validate_userspace_buffer(tidptr, sizeof(int)) != 0)
    return -EFAULT;

  current_process->set_tid_ptr = tidptr;
  return (int64_t)current_process->task.pid;
}
int64_t sys_futex(int *uaddr, int op, int val, const struct timespec *timeout,
                  int *uaddr2, int val3)
{
  (void)timeout;
  (void)uaddr2;
  (void)val3;

  if (!current_process)
    return -ESRCH;

  if (uaddr && validate_userspace_buffer(uaddr, sizeof(int)) != 0)
    return -EFAULT;

  if (op == FUTEX_WAKE)
    return 0;

  if (op == FUTEX_WAIT)
  {
    (void)val;
    sched_schedule_next();
    return 0;
  }

  return -ENOSYS;
}

/*
 * sys_getrandom - Non-cryptographic entropy for musl bring-up.
 */
int64_t sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
  uint8_t *p;
  size_t i;
  uint64_t seed;

  (void)flags;

  if (!current_process || !buf)
    return -EFAULT;

  if (buflen == 0)
    return 0;

  if (validate_userspace_buffer(buf, buflen) != 0)
    return -EFAULT;

  seed = clock_get_uptime_milliseconds() ^
         ((uint64_t)current_process->task.pid << 32);

  p = (uint8_t *)buf;
  for (i = 0; i < buflen; i++)
  {
    seed = seed * 6364136223846793005ULL + 1;
    p[i] = (uint8_t)(seed >> 33);
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
    if (validate_userspace_buffer(old_limit, 16) != 0)
      return -EFAULT;
    lim[0] = target->rlimits[resource].rlim_cur;
    lim[1] = target->rlimits[resource].rlim_max;
    if (copy_to_user(old_limit, lim, 16) != 0)
      return -EFAULT;
  }

  if (new_limit)
  {
    if (validate_userspace_buffer((void *)new_limit, 16) != 0)
      return -EFAULT;
    if (copy_from_user(lim, new_limit, 16) != 0)
      return -EFAULT;
    if (lim[0] > lim[1])
      return -EINVAL;
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

/**
 * sys_sigreturn - Return from signal handler (POSIX sigreturn)
 * @ctx: Signal context to restore (from signal frame)
 *
 * Restores CPU state saved before signal handler was invoked.
 * This allows the process to resume execution after handling a signal.
 *
 * Returns: Never returns normally (restores context and resumes execution)
 */
int64_t sys_sigreturn(struct sigcontext *ctx)
{
  if (!current_process)
    return -ESRCH;

  /* Validate context is in userspace (for USER_MODE processes) */
  if (current_process->mode == USER_MODE)
  {
    if (!ctx || validate_userspace_buffer(ctx, sizeof(struct sigcontext)) != 0)
      return -EFAULT;
    
    /* Restore context from saved context or from argument */
    struct sigcontext *restore_ctx = current_process->saved_context;
    if (!restore_ctx)
    {
      /* No saved context - use argument */
      restore_ctx = ctx;
    }
    
    /* Restore CPU state from context */
    current_process->task.r15 = restore_ctx->r15;
    current_process->task.r14 = restore_ctx->r14;
    current_process->task.r13 = restore_ctx->r13;
    current_process->task.r12 = restore_ctx->r12;
    current_process->task.rbp = restore_ctx->rbp;
    current_process->task.rbx = restore_ctx->rbx;
    current_process->task.r11 = restore_ctx->r11;
    current_process->task.r10 = restore_ctx->r10;
    current_process->task.r9 = restore_ctx->r9;
    current_process->task.r8 = restore_ctx->r8;
    current_process->task.rax = restore_ctx->rax;
    current_process->task.rcx = restore_ctx->rcx;
    current_process->task.rdx = restore_ctx->rdx;
    current_process->task.rsi = restore_ctx->rsi;
    current_process->task.rdi = restore_ctx->rdi;
    /* orig_rax not stored in task_t - not needed for context restoration */
    current_process->task.rip = restore_ctx->rip;
    current_process->task.cs = restore_ctx->cs;
    current_process->task.rflags = restore_ctx->rflags;
    current_process->task.rsp = restore_ctx->rsp;
    current_process->task.ss = restore_ctx->ss;
    
    /* Free saved context */
    if (current_process->saved_context)
    {
      kfree(current_process->saved_context);
      current_process->saved_context = NULL;
    }
    
    /* Process will resume at restored RIP on next context switch */
    return 0;  /* Should not be reached, but return 0 for safety */
  }
  
  /* KERNEL_MODE: no-op */
  return 0;
}
