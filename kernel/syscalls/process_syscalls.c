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
#include <ir0/arch_port.h>
#include <ir0/clock.h>
#include <ir0/clone.h>
#include <ir0/copy_user.h>
#include <ir0/cred.h>
#include <ir0/elf_loader.h>
#include <ir0/kexec.h>
#include <ir0/kmem.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/ktm/klog.h>
#include <ir0/mm_port.h>
#include <ir0/oops.h>
#include <ir0/path_user.h>
#include <ir0/power_manag.h>
#include <ir0/sched.h>
#include <ir0/wait.h>

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* prctl(2) options (Linux include/uapi/linux/prctl.h) */
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
 * than CONFIG_SCHEDULER_POLICY=2 ignore sched_prio, so there the nice value
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
 * legitimate negative nice is not mistaken for an error code; musl and glibc
 * undo the bias (Linux kernel/sys.c).
 */
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

/*
 * True for /proc/self/exe and for /proc/<pid>/exe naming the caller. Other
 * PIDs are deliberately not accepted: execing another process's image by
 * path is not what the standalone-shell re-exec needs, and honouring it here
 * would bypass the permission check that ran against the literal path.
 */
static int exec_path_is_proc_self_exe(const char *path)
{
	const char *p;
	pid_t pid = 0;

	if (!path || strncmp(path, "/proc/", 6) != 0)
		return 0;

	p = path + 6;
	if (strncmp(p, "self/exe", 8) == 0 && p[8] == '\0')
		return 1;

	if (*p < '0' || *p > '9')
		return 0;
	while (*p >= '0' && *p <= '9')
		pid = pid * 10 + (*p++ - '0');

	if (strncmp(p, "/exe", 4) != 0 || p[4] != '\0')
		return 0;

  return current_process && pid == current_process->task.pid;
}

/*
 * Linux fs/exec.c copy_strings(): one string, NUL included, may be
 * MAX_ARG_STRLEN (32 pages) long, and argv and envp share a bounded
 * argument space; overflowing either is E2BIG. IR0's space is 128 KiB so
 * the copies also fit the saved /proc cmdline and environ blobs. Each
 * string is its own heap block: the loader frees them once the new image
 * holds them (exec_replace_current() does not return on success).
 */
#define EXEC_ARG_STRLEN_MAX (32L * 4096L)
#define EXEC_ARG_SPACE (128L * 1024L)
#define EXEC_ARG_SLOTS 256

struct exec_args
{
  char *argv[EXEC_ARG_SLOTS];
  char *envp[EXEC_ARG_SLOTS];
  long used;
};

/* Length of the user string @u, or @max when it has no NUL within @max. */
static long exec_strnlen_user(const char *u, long max)
{
  char chunk[128];
  long len = 0;

  while (len < max)
  {
    long want = max - len;
    long n;

    if (want > (long)sizeof(chunk))
      want = (long)sizeof(chunk);
    n = strncpy_from_user(chunk, u + len, want);
    if (n < 0)
      return n;
    len += n;
    if (n < want)
      return len;
  }
  return max;
}

/* Charge @len bytes plus the NUL to the argument space and copy them. */
static char *exec_args_store(struct exec_args *args, const char *src,
                             long len, int from_user)
{
  char *dst;

  if (len >= EXEC_ARG_SPACE - args->used)
    return NULL;
  dst = kmalloc((size_t)len + 1);
  if (!dst)
    return NULL;
  if (from_user)
  {
    if (copy_from_user(dst, src, (size_t)len) != 0)
    {
      kfree(dst);
      return NULL;
    }
  }
  else
  {
    memcpy(dst, src, (size_t)len);
  }
  dst[len] = '\0';
  args->used += len + 1;
  return dst;
}

static void exec_args_release(struct exec_args *args)
{
  int i;

  for (i = 0; i < EXEC_ARG_SLOTS && args->argv[i]; i++)
    kfree(args->argv[i]);
  for (i = 0; i < EXEC_ARG_SLOTS && args->envp[i]; i++)
    kfree(args->envp[i]);
}

/*
 * Copy the NULL-terminated user vector @uvec into @out. Returns the count
 * or -EFAULT / -E2BIG / -ENOMEM.
 */
static int exec_copy_strings(struct exec_args *args, char *const *uvec,
                             char **out)
{
  int i;

  for (i = 0; i < EXEC_ARG_SLOTS - 1; i++)
  {
    const char *uptr;
    long limit;
    long len;

    if (copy_from_user(&uptr, (const char *)uvec + i * sizeof(char *),
                       sizeof(uptr)) != 0)
      return -EFAULT;
    if (!uptr)
      return i;
    limit = EXEC_ARG_SPACE - args->used;
    if (limit > EXEC_ARG_STRLEN_MAX)
      limit = EXEC_ARG_STRLEN_MAX;
    len = exec_strnlen_user(uptr, limit);
    if (len < 0)
      return (int)len;
    if (len == limit)
      return -E2BIG;
    out[i] = exec_args_store(args, uptr, len, 1);
    if (!out[i])
      return -ENOMEM;
  }
  return -E2BIG;
}

/*
 * Linux execve(2): envp == NULL → inherit the caller's environment.
 * IR0 stores the post-exec blob in saved_environ; rebuild a kernel vector.
 */
static int exec_env_from_saved(const process_t *proc, struct exec_args *args)
{
  const char *walk = proc->saved_environ;
  size_t rem = walk ? proc->saved_environ_len : 0;
  int i;

  for (i = 0; i < EXEC_ARG_SLOTS - 1 && rem > 0; i++)
  {
    size_t len = 0;

    while (len < rem && walk[len] != '\0')
      len++;
    if (len >= rem)
      break;
    if ((long)len >= EXEC_ARG_SPACE - args->used)
      return -E2BIG;
    args->envp[i] = exec_args_store(args, walk, (long)len, 0);
    if (!args->envp[i])
      return -ENOMEM;
    walk += len + 1;
    rem -= len + 1;
  }
  return i;
}

/* execve / execveat. */

int64_t sys_exec(const char *pathname,
                 char *const argv[],
                 char *const envp[])
{
  if (!current_process || !pathname)
  {
    return -EFAULT;
  }

  /* Unix-style: resolve relative against cwd; absolute under chroot. */
  char resolved_path[256];
  const char *path_to_use = pathname;
  int path_rc;

  path_rc = ir0_resolve_user_path(pathname, resolved_path, sizeof(resolved_path),
                                  current_process->cwd, current_process->root);
  if (path_rc != 0)
    return path_rc;
  path_to_use = resolved_path;

  /*
   * /proc/<pid>/exe is a symlink in Linux, resolved by the path walk before
   * the binary is ever opened. IR0's /proc is not a VFS mount and the ELF
   * loader only reads through vfs_read_file, so the substitution happens
   * here instead — the one place that still knows it is handling a path.
   * execve("/proc/self/exe") runs the caller's own image again (proc(5)).
   */
  if (exec_path_is_proc_self_exe(resolved_path))
  {
    if (!current_process->exe_path[0])
      return -ENOENT;
    path_to_use = current_process->exe_path;
  }

  struct exec_args args;
  int64_t result;

  memset(&args, 0, sizeof(args));
  result = argv ? exec_copy_strings(&args, argv, args.argv) : 0;
  if (result >= 0)
    result = envp ? exec_copy_strings(&args, envp, args.envp) :
                    exec_env_from_saved(current_process, &args);
  if (result < 0)
  {
    exec_args_release(&args);
    return result;
  }

  if (current_process->mode == USER_MODE)
  {
    KTM_CHECKPOINT(KTM_CP_PROCESS_EXEC);
    result = exec_replace_current(path_to_use, argv ? args.argv : NULL,
                                  args.envp);
  }
  else
  {
    result = kexecve(path_to_use, argv ? args.argv : NULL, args.envp);
  }
  /*
   * A successful exec_replace_current() does not return: the loader freed
   * the strings. Anything that returns here left them to us.
   */
  exec_args_release(&args);

  /*
   * A refused exec used to return to ring 3 with nothing in the log, so a
   * child that failed here was indistinguishable from one whose program
   * exited 127 on its own. Record the errno.
   */
  if (result < 0)
  {
    size_t heap_total = 0, heap_used = 0, heap_allocs = 0;

    /*
     * Heap census on refusal: exec loads the whole image with
     * kmalloc(), so -ENOMEM means either the heap is genuinely full
     * (a leak) or no contiguous block that size is left (fragmentation).
     * The used figure separates the two.
     */
    ir0_mm_alloc_stats(&heap_total, &heap_used, &heap_allocs);

    /* -ENOENT is the shell walking PATH; only real refusals are notable. */
    if (result == -ENOENT)
      klog_debug_fmt("EXEC", "exec enoent pid=%x\n",
                     (unsigned)((uint32_t)current_process->task.pid));
    else
      klog_notice_fmt("EXEC", "exec refused pid=%x err=%x path=%s heap_used=%x heap_total=%x\n",
                    (unsigned)((uint32_t)current_process->task.pid),
                    (unsigned)(-result),
                    path_to_use ? path_to_use : "(null)",
                    (unsigned)heap_used,
                    (unsigned)heap_total);
  }

  return result;
}

int64_t sys_execveat(int dirfd, const char *pathname, char *const argv[],
		     char *const envp[], int flags)
{
  fd_entry_t *fd_table;
  char first;

  if (!current_process || !pathname)
    return -EFAULT;
  if (flags & ~IR0_AT_EMPTY_PATH)
    return -EINVAL;
  if (copy_from_user(&first, pathname, sizeof(first)) != 0)
    return -EFAULT;

  if (first != '\0')
  {
    if (dirfd != IR0_AT_FDCWD)
      return -EOPNOTSUPP;
    return sys_exec(pathname, argv, envp);
  }
  if (!(flags & IR0_AT_EMPTY_PATH))
    return -ENOENT;
  if (dirfd < 0 || dirfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;
  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[dirfd].in_use ||
      fd_table[dirfd].path[0] == '\0')
    return -EBADF;
  return sys_exec(fd_table[dirfd].path, argv, envp);
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
	return (int64_t)vfork_process(NULL);
}

/*
 * sys_clone — Linux __NR_clone (56).
 * CLONE_VFORK|CLONE_VM: vfork semantics (no CLONE_THREAD).
 * CLONE_THREAD|CLONE_VM: lightweight thread sharing the caller's mm.
 * Otherwise duplicates the address space like fork().
 * stack is the child user RSP (musl __clone / posix_spawn); NULL keeps the
 * parent's stack (classic vfork).
 */
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
                  int *child_tid, unsigned long tls)
{
	if (!current_process)
		return -ESRCH;

	if (flags & CLONE_VFORK)
	{
		if ((flags & CLONE_THREAD) || !(flags & CLONE_VM))
			return -EINVAL;
		return (int64_t)vfork_process(stack);
	}

	if (flags & CLONE_THREAD)
		return (int64_t)clone_thread(flags, stack, parent_tid, child_tid, tls);

	(void)parent_tid;
	(void)child_tid;
	(void)tls;
	return fork_with_stack(stack);
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

    /* Read the wait arguments from the syscall frame (pt_regs). */
    wait_pid = (pid_t)process_syscall_arg(current_process, 0);
    wait_opts = (int)process_syscall_arg(current_process, 2);
    pid = wait_pid;
    options = wait_opts;
  }

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

int64_t sys_waitpid(pid_t pid, int *status, int options)
{
  return sys_wait4(pid, status, options, NULL);
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
 * sys_set_tid_address - Linux set_tid_address(2) stub for musl.
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
 * sys_getrandom - Non-cryptographic entropy for musl bring-up.
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
