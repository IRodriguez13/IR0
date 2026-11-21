/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: exec.c
 * Description: execve(2)/execveat(2): argument and environment copy, shared
 * mm detach and FD_CLOEXEC close before image replace.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/mm_struct.h>
#include <ir0/paging.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/sysv_shm.h>
#include <ir0/copy_user.h>
#include <ir0/elf_loader.h>
#include <ir0/files_struct.h>
#include <ir0/kmem.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/ktm/klog.h>
#include <ir0/mm_port.h>
#include <ir0/path_user.h>
#include <string.h>

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

int exec_detach_shared_mm(process_t *proc)
{
	mm_struct_t *old;
	mm_struct_t *fresh;
	address_space_root_t root;

	if (!proc || !proc->mm)
		return -EINVAL;
	if (mm_users(proc->mm) <= 1)
		return 0;

	fresh = mm_create();
	if (!fresh)
		return -ENOMEM;

	root = process_mm_create_root();
	if (!root)
	{
		mm_put(fresh);
		return -ENOMEM;
	}

	mm_init_root(fresh, root, 1);
	old = proc->mm;
	/*
	 * Still the shared tables. Unmap this task's attaches before the
	 * fresh root is installed; a failure above has not switched yet.
	 */
	sysv_shm_detach_process(proc);
	/*
	 * Linux exec_mmap: bind + activate the private mm, then
	 * complete_vfork_done, then mmput(old). switch_to_user() must
	 * load the new root so a vfork child does not fetch the new
	 * image against the parent's tables.
	 */
	process_mm_bind(proc, fresh);
	process_set_mm_root(proc, (uint64_t)virt_to_phys(root));
	if (proc == current_process)
		paging_activate_address_space(virt_to_phys(root));
	process_vfork_complete(proc);
	mm_put(old);
	return 0;
}

void process_exec_close_cloexec(process_t *p)
{
	fd_entry_t *table;
	int i;

	if (!p)
		return;

	table = process_fd_table(p);
	if (!table)
		return;

	for (i = 3; i < process_fd_span(p); i++)
	{
		fd_entry_t *e = &table[i];

		if (!e->in_use)
			continue;
		if (!(e->fd_flags & FD_CLOEXEC))
			continue;
		(void)process_close_fd(p, i);
	}
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
	if (dirfd < 0 || dirfd >= ir0_fd_span())
		return -EBADF;
	fd_table = get_process_fd_table();
	if (!fd_table || !fd_table[dirfd].in_use ||
	    fd_table[dirfd].path[0] == '\0')
		return -EBADF;
	return sys_exec(fd_table[dirfd].path, argv, envp);
}
