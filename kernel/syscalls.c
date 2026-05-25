// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: System call POSIX ABI interface source module
 * 
 */

#include <ir0/console.h>
#include <ir0/fase58j_diag.h>
#include "syscalls.h"
#include "syscalls/fs_syscalls.h"
#include "process.h"
#include <ir0/bits/syscall_linux.h>
#include <ir0/utsname.h>
#include <mm/pmm.h>
#include <mm/allocator.h>
#include <ir0/clock.h>
#include <config.h>
#include <ir0/version.h>
#include <ir0/serial_io.h>
#include <ir0/console_backend.h>
#include <kernel/elf_loader.h>
#include <kernel/kernel.h>
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <ir0/validation.h>
#include <ir0/vga.h>
#include <ir0/stat.h>
#include <ir0/scheduler_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ir0/oops.h>
#include <ir0/copy_user.h>
#include <ir0/keyboard.h>
#include <fs/vfs.h>
#include <ir0/path.h>
#include <ir0/driver.h>
#include <ir0/errno.h>
#include <ir0/copy_user.h>
#include <ir0/path_user.h>
#include <ir0/path_routed.h>
#include <ir0/uio.h>
#include <ir0/utimens.h>
#include <ir0/fase50_debug.h>
#include <ir0/fase51_debug.h>
#include <ir0/fase52_debug.h>
#include <ir0/fb.h>
#include <ir0/permissions.h>
#include <ir0/chmod.h>
#include <ir0/fcntl.h>
#include <ir0/procfs.h>
#include <ir0/devfs.h>
#include <ir0/sysfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/signals.h>
#include <ir0/pipe.h>
#include <ir0/poll.h>
#include <ir0/time.h>
#include <ir0/video_backend.h>
#include <ir0/rtc.h>
#include <ir0/arch_port.h>
#include "syscalls/syscalls_glue.h"

#define ARCH_SET_FS 0x1002
#define ARCH_GET_FS 0x1003

/* Forward declarations */
fd_entry_t *get_process_fd_table(void);
int64_t sys_unlink(const char *pathname);
int64_t sys_truncate(const char *pathname, off_t length);
static void init_syscall_table(void);
static void fase39_dump_current_vmas(const char *tag);
static int64_t sys_pipe_install(int pipefd[2], int flags);
int64_t sys_pipe2(int pipefd[2], int flags);

static int devfs_initialized = 0;

/*
 * read(0) bloqueante: procesos esperando teclado.
 * Se despiertan desde stdin_wake_check en el main loop.
 */
#define MAX_STDIN_WAITERS 8
static process_t *stdin_waiters[MAX_STDIN_WAITERS];

#define MAX_PIPE_WAITERS 32

struct pipe_waiter
{
	process_t *proc;
	pipe_t *pipe;
	int waiting_read;
};

static struct pipe_waiter pipe_waiters[MAX_PIPE_WAITERS];
static uint64_t fase48_fd_created;
static uint64_t fase48_fd_destroyed;
static uint64_t fase48_blocked_readers;
static uint64_t fase48_blocked_writers;

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

void fase50b_dump_bytes(const char *label, const void *buf, size_t n)
{
#if CONFIG_DEBUG_FASE50
	const uint8_t *b = (const uint8_t *)buf;
	size_t i;

	serial_print(label ? label : "[FASE50B][BYTES]");
	serial_print(" n=");
	serial_print_hex64((uint64_t)n);
	serial_print(" hex=");
	for (i = 0; i < n && i < 32; i++)
	{
		if (i > 0)
			serial_print(" ");
		serial_print_hex64((uint64_t)b[i]);
	}
	serial_print(" ascii=");
	for (i = 0; i < n && i < 32; i++)
	{
		char c = (char)b[i];

		if (c >= 32 && c <= 126)
			serial_putchar(c);
		else
			serial_print(".");
	}
	serial_print("\n");
#else
	(void)label;
	(void)buf;
	(void)n;
#endif
}

static int fase50b_peek_user(uint64_t *pml4, uintptr_t user_addr,
			     uint8_t *scratch, size_t n)
{
	size_t i;

	if (!pml4 || !scratch || n == 0)
		return -1;

	for (i = 0; i < n; i++)
	{
		uintptr_t page = user_addr & (uintptr_t)PAGE_FRAME_MASK;
		uint64_t *pte = paging_get_pte(pml4, page);
		uintptr_t phys;
		uint8_t byte;

		if (!pte || !(*pte & PAGE_PRESENT))
			return -1;

		phys = (uintptr_t)(*pte & PAGE_PTE_PFN_MASK);
		byte = *(const uint8_t *)(phys + (user_addr & 0xFFFU));
		scratch[i] = byte;
		user_addr++;
	}
	return 0;
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

void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
			 uint64_t *blocked_readers, uint64_t *blocked_writers)
{
	if (created)
		*created = fase48_fd_created;
	if (destroyed)
		*destroyed = fase48_fd_destroyed;
	if (blocked_readers)
		*blocked_readers = fase48_blocked_readers;
	if (blocked_writers)
		*blocked_writers = fase48_blocked_writers;
}

void fase48_note_fd_created(void)
{
	fase48_fd_created++;
}

void fase48_note_fd_destroyed(void)
{
	fase48_fd_destroyed++;
}

int pipe_wait(process_t *proc, pipe_t *pipe, int waiting_read)
{
	unsigned int i;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		if (!pipe_waiters[i].proc)
		{
			pipe_waiters[i].proc = proc;
			pipe_waiters[i].pipe = pipe;
			pipe_waiters[i].waiting_read = waiting_read;
			if (waiting_read)
				fase48_blocked_readers++;
			else
				fase48_blocked_writers++;
			if (waiting_read)
				pipe_fase49_note_read_sleep(pipe);
			if (proc->mode == USER_MODE)
			{
				serial_print("[FASE50B][READ_BLOCK] pid=");
				serial_print_hex32((uint32_t)proc->task.pid);
				serial_print(" fd=");
				serial_print_hex64(proc->syscall_frame.rdi);
				serial_print(" rsi=");
				serial_print_hex64(proc->syscall_frame.rsi);
				serial_print(" rdx=");
				serial_print_hex64(proc->syscall_frame.rdx);
				serial_print(" pipe_id=");
				serial_print_hex64(pipe ? pipe->pipe_id : 0);
				serial_print("\n");
				process_arm_blocked_syscall_resume(proc, 0);
			}
			proc->state = PROCESS_BLOCKED;
			sched_schedule_next();
			if (waiting_read)
				pipe_fase49_note_read_wake(pipe);
			pipe_waiters[i].proc = NULL;
			pipe_waiters[i].pipe = NULL;
			pipe_waiters[i].waiting_read = 0;
			return 0;
		}
	}
	return -EAGAIN;
}

static void pipe_wake_stage_user_read(process_t *proc, pipe_t *pipe)
{
	char kbuf[PIPE_SIZE];
	uint8_t user_before[32];
	uint8_t user_after[32];
	uintptr_t user_buf;
	size_t req;
	int n;
	int copy_ret;
	int peek_n;

	if (!proc || !pipe || !proc->irq_frame_saved || pipe->count == 0)
		return;

	user_buf = (uintptr_t)proc->syscall_frame.rsi;
	req = proc->syscall_frame.rdx;
	if (req == 0 || req > sizeof(kbuf))
		req = sizeof(kbuf);

	serial_print("[FASE50B][PIPE_WAKE] pipe_id=");
	serial_print_hex64(pipe->pipe_id);
	serial_print(" target_pid=");
	serial_print_hex32((uint32_t)proc->task.pid);
	serial_print(" waiter_pid=");
	serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
	serial_print(" saved_fd=");
	serial_print_hex64(proc->syscall_frame.rdi);
	serial_print(" saved_rsi=");
	serial_print_hex64((uint64_t)user_buf);
	serial_print(" saved_rdx=");
	serial_print_hex64((uint64_t)req);
	serial_print(" pipe_bytes=");
	serial_print_hex64((uint64_t)pipe->count);
	serial_print(" target_pml4=");
	serial_print_hex64((uint64_t)(uintptr_t)proc->page_directory);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");

	peek_n = (req < (size_t)sizeof(user_before)) ? (int)req : (int)sizeof(user_before);
	if (fase50b_peek_user(proc->page_directory, user_buf, user_before, (size_t)peek_n) == 0)
		fase50b_dump_bytes("[FASE50B][PIPE_WAKE] user_before", user_before, (size_t)peek_n);

	n = pipe_read(pipe, kbuf, req);
	if (n <= 0)
	{
		serial_print("[FASE50B][PIPE_WAKE] pipe_read_ret=");
		serial_print_hex64((uint64_t)(int64_t)n);
		serial_print("\n");
		return;
	}

	fase50b_dump_bytes("[FASE50B][PIPE_WAKE] kbuf", kbuf, (size_t)n);

	if (!proc->page_directory)
	{
		serial_print("[FASE50B][CLASSIFY] PIPE_WAKE_COPY_BAD_USERBUF\n");
		return;
	}

	copy_ret = copy_to_user_region_in_directory(proc->page_directory, user_buf,
						      kbuf, (size_t)n);
	serial_print("[FASE50B][PIPE_WAKE] copy_ret=");
	serial_print_hex64((uint64_t)(int64_t)copy_ret);
	serial_print(" copied=");
	serial_print_hex64((uint64_t)(int64_t)n);
	serial_print(" resume_rax=");
	serial_print_hex64((uint64_t)(int64_t)n);
	serial_print("\n");

	if (copy_ret != 0)
	{
		serial_print("[FASE50B][CLASSIFY] PIPE_WAKE_COPY_BAD_USERBUF\n");
		return;
	}

	if (fase50b_peek_user(proc->page_directory, user_buf, user_after, (size_t)peek_n) == 0)
		fase50b_dump_bytes("[FASE50B][PIPE_WAKE] user_after", user_after, (size_t)peek_n);

	proc->syscall_resume_rax = (uint64_t)n;
	serial_print("[FASE50B][CLASSIFY] PIPE_WAKE_COPY_OK\n");
}

void pipe_wake_all(pipe_t *pipe)
{
	unsigned int i;

	if (!pipe)
		return;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		struct pipe_waiter *w = &pipe_waiters[i];

		if (!w->proc || w->pipe != pipe)
			continue;

		if (w->waiting_read)
		{
			if (pipe->count > 0 || pipe->writers <= 0)
			{
				pipe_fase49_note_read_wake(pipe);
				if (w->proc && w->proc->mode == USER_MODE && w->proc->irq_frame_saved)
					pipe_wake_stage_user_read(w->proc, pipe);
				w->proc->state = PROCESS_READY;
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
		else
		{
			if (pipe->readers <= 0 || pipe->count < PIPE_SIZE)
			{
				pipe_fase49_note_write_wake(pipe);
				w->proc->state = PROCESS_READY;
				w->proc = NULL;
				w->pipe = NULL;
				w->waiting_read = 0;
			}
		}
	}
}

void pipe_wake_check(void)
{
	unsigned int i;

	for (i = 0; i < MAX_PIPE_WAITERS; i++)
	{
		struct pipe_waiter *w = &pipe_waiters[i];
		pipe_t *pipe;

		if (!w->proc || !w->pipe)
			continue;

		pipe = w->pipe;
		if (w->waiting_read)
		{
			if (pipe->count > 0 || pipe->writers <= 0)
			{
				pipe_fase49_note_read_wake(pipe);
				if (w->proc && w->proc->mode == USER_MODE && w->proc->irq_frame_saved)
					pipe_wake_stage_user_read(w->proc, pipe);
				w->proc->state = PROCESS_READY;
			}
		}
		else
		{
			if (pipe->readers <= 0 || pipe->count < PIPE_SIZE)
			{
				pipe_fase49_note_write_wake(pipe);
				w->proc->state = PROCESS_READY;
			}
		}
	}
}

void ensure_devfs_init(void)
{
  if (!devfs_initialized)
  {
    devfs_init();
    devfs_initialized = 1;
  }
}

/**
 * validate_userspace_string - Validate that a string argument is in userspace
 * @str: String pointer to validate
 * @max_len: Maximum expected string length
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_string(const char *str, size_t max_len)
{
  if (!current_process)
    return -ESRCH;
  
  /* For KERNEL_MODE processes (like dbgshell), allow addresses in process stack/heap range
   * This allows debug_bins/ commands to work while still simulating userspace behavior
   */
  if (current_process->mode == KERNEL_MODE)
  {
    /* Allow if address is in process's stack or heap region */
    uint64_t addr = (uint64_t)str;
    if (addr >= current_process->stack_start && 
        addr < current_process->stack_start + current_process->stack_size)
      return 0;
    if (current_process->heap_start > 0 &&
        addr >= current_process->heap_start && 
        addr < current_process->heap_end)
      return 0;
    /* Also allow if it's a valid user address (for compatibility) */
    if (is_user_address(str, max_len))
      return 0;
    /* For KERNEL_MODE, be more lenient - allow kernel addresses from current process stack */
    return 0;  /* Allow kernel space addresses for debug_bins/ simulation */
  }
  
  /* USER_MODE: strict validation - must be in userspace */
  if (!is_user_address(str, max_len))
    return -EFAULT;
  
  return 0;
}

/**
 * validate_userspace_buffer - Validate that a buffer argument is in userspace
 * @buf: Buffer pointer to validate
 * @size: Size of buffer
 * Returns: 0 if valid, -EFAULT if invalid
 */
int validate_userspace_buffer(const void *buf, size_t size)
{
  if (!current_process)
    return -ESRCH;
  
  /* For KERNEL_MODE processes (like dbgshell), allow addresses in process stack/heap range
   * This allows debug_bins/ commands to work while still simulating userspace behavior
   */
  if (current_process->mode == KERNEL_MODE)
  {
    /* Allow if address is in process's stack or heap region */
    uint64_t addr = (uint64_t)buf;
    if (addr >= current_process->stack_start && 
        addr + size <= current_process->stack_start + current_process->stack_size)
      return 0;
    if (current_process->heap_start > 0 &&
        addr >= current_process->heap_start && 
        addr + size <= current_process->heap_end)
      return 0;
    /* Also allow if it's a valid user address (for compatibility) */
    if (is_user_address(buf, size))
      return 0;
    /* For KERNEL_MODE, be more lenient - allow kernel addresses from current process stack */
    return 0;  /* Allow kernel space addresses for debug_bins/ simulation */
  }
  
  /* USER_MODE: strict validation - must be in userspace */
  if (!is_user_address(buf, size))
    return -EFAULT;
  
  return 0;
}

int stdio_is_redirected(fd_entry_t *fd_table, int fd)
{
  if (!fd_table || fd < STDIN_FILENO || fd > STDERR_FILENO)
    return 0;
  if (!fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_devfs)
    return 1;
  if (fd_table[fd].is_pipe)
    return 1;
  if (fd_table[fd].vfs_file)
    return 1;
  return 0;
}



int64_t syscalls_read_stdio_stdin(void *buf, size_t count)
{
  char kbuf[256];
  size_t max_read = (count < sizeof(kbuf)) ? count : sizeof(kbuf);
  int64_t ret;

  if (!current_process || !buf)
    return -EFAULT;

  ret = ir0_console_read(kbuf, max_read, 0);
  if (ret <= 0)
    return ret;
  if (copy_to_user(buf, kbuf, (size_t)ret) != 0)
    return -EFAULT;
  return ret;
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

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
  char resolved[256];
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_mkdir(resolved, (int)mode);
}

/*
 * execve(2) contract (Linux x86-64 ABI):
 *   argv and envp are NULL-terminated arrays of pointers to NUL-terminated
 *   strings. The kernel MUST NOT perform a bulk fixed-size copy of the
 *   pointer arrays from userspace, because nothing guarantees that the
 *   vectors extend up to MAX_ARGS slots; reading past the last NULL can
 *   cross unmapped pages near the stack top and panic the kernel.
 *
 *   This implementation reads pointers one at a time, validating each
 *   slot is mapped, and returns:
 *     -EFAULT  on first invalid/unmapped pointer
 *     -E2BIG   when the limit (MAX_ARGS / MAX_ENV) is exceeded
 *     0        otherwise
 */
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

  if (current_process->mode == USER_MODE)
  {
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

int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype)
{
  const char *mount_fstype;
  int dev_is_pseudo = 0;

  if (!current_process)
    return -ESRCH;

  if (!dev || !mountpoint)
    return -EFAULT;

  /* Validate arguments are in userspace (for USER_MODE processes) */
  if (validate_userspace_string(dev, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(mountpoint, 256) != 0)
    return -EFAULT;
  if (fstype && validate_userspace_string(fstype, 32) != 0)
    return -EFAULT;

  /* Use configured default filesystem if userspace passes NULL/empty fstype. */
  mount_fstype = (fstype && *fstype) ? fstype : CONFIG_ROOT_FILESYSTEM;

  /* tmpfs accepts pseudo device strings for Unix-like parity (e.g. none). */
  if (strcmp(mount_fstype, "tmpfs") == 0 &&
      (strcmp(dev, "none") == 0 || strcmp(dev, "tmpfs") == 0))
  {
    dev_is_pseudo = 1;
  }

  /* Validate device path unless pseudo device was allowed. */
  if (!dev_is_pseudo && (dev[0] != '/' || strlen(dev) >= 256)) {
    sys_write(STDERR_FILENO, "mount: invalid device path\n", 27);
    return -EINVAL;
  }

  /* Validate mountpoint path */
  if (mountpoint[0] != '/' || strlen(mountpoint) >= 256) {
    sys_write(STDERR_FILENO, "mount: invalid mount point\n", 27);
    return -EINVAL;
  }

  /* Check if mountpoint exists and is a directory */
  stat_t st;
  if (vfs_stat(mountpoint, &st) < 0) {
    sys_write(STDERR_FILENO, "mount: mount point does not exist\n", 34);
    return -ENOENT;
  }
  if (!S_ISDIR(st.st_mode)) {
    sys_write(STDERR_FILENO, "mount: mount point is not a directory\n", 38);
    return -ENOTDIR;
  }
  int ret = vfs_mount(dev, mountpoint, mount_fstype);
  if (ret < 0) {
    /* Report specific error */
    sys_write(STDERR_FILENO, "mount: failed to mount ", 22);
    sys_write(STDERR_FILENO, mount_fstype, strlen(mount_fstype));
    sys_write(STDERR_FILENO, " filesystem\n", 12);
    return ret;
  }

  return ret;
}

int64_t sys_umount(const char *target, int flags)
{
  if (!current_process)
    return -ESRCH;
  if (!target)
    return -EFAULT;

  if (validate_userspace_string(target, 256) != 0)
    return -EFAULT;

  if (flags != 0)
    return -EINVAL;

  if (target[0] != '/' || strlen(target) >= 256)
  {
    sys_write(STDERR_FILENO, "umount: invalid target path\n", 28);
    return -EINVAL;
  }

  stat_t st;
  if (vfs_stat(target, &st) < 0)
  {
    sys_write(STDERR_FILENO, "umount: target path does not exist\n", 35);
    return -ENOENT;
  }

  return vfs_umount(target);
}

int64_t sys_chmod(const char *path, mode_t mode)
{
  if (!current_process || !path)
    return -EFAULT;

  /* Validate path is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(path, 256) != 0)
    return -EFAULT;

  /* Resolve relative paths against cwd */
  char resolved[256];
  const char *path_to_use = path;
  if (!is_absolute_path(path))
  {
    if (join_paths(current_process->cwd, path, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(path, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }

  stat_t st;
  int sret = vfs_stat(path_to_use, &st);
  if (sret != 0)
    return sret;

  if (current_process->euid != ROOT_UID && current_process->euid != st.st_uid)
    return -EPERM;

  return chmod(path_to_use, mode);
}

int64_t sys_chown(const char *path, uid_t owner, gid_t group)
{
  if (!current_process || !path)
    return -EFAULT;
  if (validate_userspace_string(path, 256) != 0)
    return -EFAULT;

  /* Resolve relative paths against cwd */
  char resolved[256];
  const char *path_to_use = path;
  if (!is_absolute_path(path))
  {
    if (join_paths(current_process->cwd, path, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(path, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }

  if (current_process->euid != ROOT_UID)
    return -EPERM;

  return vfs_chown(path_to_use, owner, group);
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
  char old_resolved[256];
  char new_resolved[256];
  int rc;

  if (!current_process || !oldpath || !newpath)
    return -EFAULT;

  if (validate_userspace_string(oldpath, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(newpath, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(oldpath, old_resolved, sizeof(old_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  rc = ir0_resolve_user_path(newpath, new_resolved, sizeof(new_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_link(old_resolved, new_resolved);
}

/**
 * sys_rename - Rename/move file (POSIX)
 * @oldpath: Source path
 * @newpath: Destination path
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_rename(const char *oldpath, const char *newpath)
{
  char old_resolved[256];
  char new_resolved[256];
  int rc;

  if (!current_process || !oldpath || !newpath)
    return -EFAULT;

  if (validate_userspace_string(oldpath, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(newpath, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(oldpath, old_resolved, sizeof(old_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  rc = ir0_resolve_user_path(newpath, new_resolved, sizeof(new_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_rename(old_resolved, new_resolved);
}

/**
 * sys_uname - Get system information (POSIX/Linux)
 * @buf: struct utsname buffer
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_uname(struct utsname *buf)
{
  if (!current_process || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, sizeof(struct utsname)) != 0)
    return -EFAULT;

  memset(buf, 0, sizeof(struct utsname));
  strncpy(buf->sysname, "IR0", _UTSNAME_LENGTH - 1);
  strncpy(buf->nodename, IR0_BUILD_HOST, _UTSNAME_LENGTH - 1);
  strncpy(buf->release, IR0_VERSION_STRING, _UTSNAME_LENGTH - 1);
  strncpy(buf->version, IR0_BUILD_INFO, _UTSNAME_LENGTH - 1);
  strncpy(buf->machine, "x86_64", _UTSNAME_LENGTH - 1);
  return 0;
}

/**
 * sys_access - Check file access (POSIX)
 * @pathname: Path to check
 * @mode: F_OK, R_OK, W_OK, X_OK
 *
 * Returns: 0 if accessible, negative error code on failure
 */
int64_t sys_access(const char *pathname, int mode)
{
  int64_t rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  char resolved[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else if (normalize_path(pathname, resolved, sizeof(resolved)) == 0)
  {
    path_to_use = resolved;
  }

  rc = ir0_access_path_routed(path_to_use, mode,
                              (uid_t)current_process->euid,
                              (gid_t)current_process->egid);
  fase52_dbg_access(path_to_use, mode, (int)rc);
  return rc;
}

int64_t sys_faccessat(int dirfd, const char *pathname, int mode, int flags)
{
  char resolved[256];
  int rc;
  int masked_flags;

  if (!current_process || !pathname)
    return -EFAULT;
  masked_flags = flags & ~(IR0_AT_EACCESS | IR0_AT_SYMLINK_NOFOLLOW);
  if (masked_flags != 0)
    return -EINVAL;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path_at(dirfd, pathname, resolved, sizeof(resolved),
                                current_process->cwd);
  if (rc != 0)
    return rc;

  /*
   * IR0 currently evaluates access permissions against effective IDs.
   * AT_EACCESS is accepted and maps to this same behavior.
   * AT_SYMLINK_NOFOLLOW is accepted as a no-op because symlinks are not implemented.
   */
  return ir0_access_path_routed(resolved, mode,
                                (uid_t)current_process->euid,
                                (gid_t)current_process->egid);
}

/**
 * sys_dup - Duplicate file descriptor to lowest free fd
 *
 * Returns: new fd on success, negative error code on failure
 */
int64_t sys_dup(int oldfd)
{
  if (!current_process)
    return -ESRCH;

  if (oldfd < 0 || oldfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[oldfd].in_use)
    return -EBADF;

  for (int i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
      return sys_dup2(oldfd, i);
  }
  return -EMFILE;
}

/**
 * sys_rmdir - Remove directory (POSIX)
 * @pathname: Path to directory to remove
 *
 * POSIX: only removes empty directories (ENOTEMPTY if non-empty).
 * Avoids vfs_rmdir_recursive to prevent VM hang on disk I/O.
 *
 * Returns: 0 on success, negative error code on failure
 */
int64_t sys_rmdir(const char *pathname)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_rmdir(resolved);
}

fd_entry_t *get_process_fd_table(void)
{
  if (!current_process)
    return NULL;
  /*
   * La tabla se inicializa en spawn() y en start_init_process(); no usar un
   * flag estático global (rompía si el primer syscall no era del proceso init).
   */
  return current_process->fd_table;
}

/* poll(2): esperar eventos en fd. Bloquea hasta que haya datos o timeout.      */

#define MAX_POLL_WAITERS  16
#define MAX_POLL_NFDS     32

struct poll_waiter {
  process_t *proc;
  struct pollfd *user_fds;
  unsigned int nfds;
  struct pollfd *kfds;
  uint64_t timeout_expire;
  int woken;
  int ready_count;
};

static struct poll_waiter poll_waiters[MAX_POLL_WAITERS];

/*
 * nanosleep(2) waiters - OSDev Time And Date
 * Block process until wake_time_ms; poll_wake_check wakes them.
 */
#define MAX_SLEEP_WAITERS 16
struct sleep_waiter {
    process_t *proc;
    uint64_t wake_time_ms;
};

static struct sleep_waiter sleep_waiters[MAX_SLEEP_WAITERS];

/**
 * fd_can_read - Comprueba si el fd tiene datos para leer (sin bloquear).
 */
static int fd_can_read(int fd)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (fd == STDIN_FILENO && !stdio_is_redirected(fd_table, fd))
    return ir0_console_input_ready() ? 1 : 0;
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !stdio_is_redirected(fd_table, fd))
    return 0;
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
    return 1;
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    pid_t pid = current_process ? current_process->task.pid : 0;
    return devfs_fd_can_read((uint32_t)(fd - FD_DEV_BASE), pid);
  }
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return 1;
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_devfs)
  {
    pid_t pid = current_process ? current_process->task.pid : 0;

    return devfs_fd_can_read(fd_table[fd].dev_device_id, pid) ? 1 : 0;
  }
  if (fd_table[fd].is_pipe && fd_table[fd].pipe_end == 0) {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    return (pipe && (pipe->count > 0 || pipe->writers <= 0)) ? 1 : 0;
  }
  if (fd_table[fd].flags & (O_RDONLY | O_RDWR))
    return 1;
  return 0;
}

/**
 * fd_can_write - Comprueba si se puede escribir en el fd.
 */
static int fd_can_write(int fd)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (fd == STDIN_FILENO && !stdio_is_redirected(fd_table, fd))
    return 0;
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !stdio_is_redirected(fd_table, fd))
    return 1;
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
    return 0;
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    pid_t pid = current_process ? current_process->task.pid : 0;
    return devfs_fd_can_write((uint32_t)(fd - FD_DEV_BASE), pid);
  }
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return 0;
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_pipe && fd_table[fd].pipe_end == 1) {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    return (pipe && pipe->readers > 0 && pipe->count < PIPE_SIZE) ? 1 : 0;
  }
  if (fd_table[fd].flags & (O_WRONLY | O_RDWR))
    return 1;
  return 0;
}

/**
 * poll_check_ready - Rellena revents y devuelve cuántos fd tienen eventos.
 */
static int poll_check_ready(struct pollfd *fds, unsigned int nfds)
{
  int count = 0;
  unsigned int i;
  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0)
      continue;
    if ((fds[i].events & POLLIN) && fd_can_read(fds[i].fd))
      fds[i].revents |= POLLIN;
    if ((fds[i].events & POLLOUT) && fd_can_write(fds[i].fd))
      fds[i].revents |= POLLOUT;
    if (fds[i].revents)
      count++;
  }
  return count;
}

int64_t sys_poll(struct pollfd *user_fds, unsigned int nfds, int timeout_ms)
{
  if (!current_process)
    return -ESRCH;
  if (!user_fds)
    return -EFAULT;
  if (nfds == 0 || nfds > MAX_POLL_NFDS)
    return -EINVAL;
  if (validate_userspace_buffer(user_fds, nfds * sizeof(struct pollfd)) != 0)
    return -EFAULT;

  struct pollfd *kfds = (struct pollfd *)kmalloc_try(nfds * sizeof(struct pollfd));
  if (!kfds)
    return -ENOMEM;
  if (copy_from_user(kfds, user_fds, nfds * sizeof(struct pollfd)) != 0) {
    kfree(kfds);
    return -EFAULT;
  }

  int ready = poll_check_ready(kfds, nfds);
  if (ready > 0 || timeout_ms == 0) {
    if (copy_to_user(user_fds, kfds, nfds * sizeof(struct pollfd)) != 0) {
      kfree(kfds);
      return -EFAULT;
    }
    kfree(kfds);
    return ready;
  }

  uint64_t now = clock_get_uptime_milliseconds();
  uint64_t expire = (timeout_ms < 0) ? (uint64_t)-1 : (now + (uint64_t)timeout_ms);

  struct poll_waiter *w = NULL;
  unsigned int i;
  for (i = 0; i < MAX_POLL_WAITERS; i++) {
    if (!poll_waiters[i].proc) {
      w = &poll_waiters[i];
      break;
    }
  }
  if (!w) {
    kfree(kfds);
    return -EAGAIN;
  }
  w->proc = current_process;
  w->user_fds = user_fds;
  w->nfds = nfds;
  w->kfds = kfds;
  w->timeout_expire = expire;
  w->woken = 0;
  w->ready_count = 0;
  current_process->poll_waiter = w;

  current_process->state = PROCESS_BLOCKED;
  while (current_process->state == PROCESS_BLOCKED)
  {
    sched_schedule_next();
    if (current_process->state != PROCESS_BLOCKED)
      break;
    kernel_idle_poll();
  }

  /* Vuelta del scheduler: despertados por poll_wake_check */
  w = (struct poll_waiter *)current_process->poll_waiter;
  current_process->poll_waiter = NULL;
  if (w) {
    ready = w->ready_count;
    kfree(w->kfds);
    w->proc = NULL;
    w->kfds = NULL;
    w->user_fds = NULL;
    w->nfds = 0;
    w->woken = 0;
    w->ready_count = 0;
  }
  return (int64_t)ready;
}

/**
 * poll_wake_check - Revisar waiters de poll; despertar si hay datos o timeout.
 * Llamar desde el bucle principal (main.c) tras net_poll/bluetooth_poll.
 */
void poll_wake_check(void)
{
  process_t *saved_current = current_process;
  uint64_t now = clock_get_uptime_milliseconds();
  unsigned int i;
  int woke = 0;

  for (i = 0; i < MAX_POLL_WAITERS; i++) {
    
    struct poll_waiter *w = &poll_waiters[i];
    
    if (!w->proc)
      continue;
    current_process = w->proc;
    int ready = poll_check_ready(w->kfds, w->nfds);
    int timeout = (w->timeout_expire != (uint64_t)-1 && now >= w->timeout_expire);
    if (ready > 0 || timeout) {
      w->ready_count = ready;
      w->woken = 1;
      if (copy_to_user(w->user_fds, w->kfds, w->nfds * sizeof(struct pollfd)) != 0)
        w->ready_count = -EFAULT;
      w->proc->state = PROCESS_READY;
      woke = 1;
    }
  }
  current_process = saved_current;
  if (woke)
    sched_schedule_next();
}

/**
 * stdin_wake_check - Wake processes blocked on read(0) when keyboard has data.
 * Called from main loop.
 */
void stdin_wake_check(void)
{
  int woke_stdin = 0;
  int woke_tty = 0;

  if (!keyboard_buffer_has_data() && !ir0_console_input_ready())
    return;

  woke_tty = ir0_console_wake_readers();

  for (int i = 0; i < MAX_STDIN_WAITERS; i++)
  {
    if (stdin_waiters[i])
    {
      stdin_waiters[i]->state = PROCESS_READY;
      stdin_waiters[i] = NULL;
      woke_stdin = 1;
    }
  }

  if (woke_stdin || woke_tty)
    sched_schedule_next();
}

/**
 * sleep_wake_check - Wake processes whose nanosleep has expired.
 * Called from main loop (OSDev Time And Date).
 */
void sleep_wake_check(void)
{
  uint64_t now = clock_get_uptime_milliseconds();
  for (unsigned int i = 0; i < MAX_SLEEP_WAITERS; i++)
  {
    struct sleep_waiter *w = &sleep_waiters[i];
    if (!w->proc)
      continue;
    if (now >= w->wake_time_ms)
    {
      w->proc->state = PROCESS_READY;
      w->proc = NULL;
    }
  }
}

/**
 * sys_nanosleep - Sleep for specified time (POSIX, OSDev Time And Date)
 * @req: Requested sleep duration (seconds + nanoseconds)
 * @rem: Remaining time if interrupted (optional, can be NULL)
 *
 * Blocks the process until the requested time has elapsed.
 * Resolution is limited to clock tick (~1ms). EINTR not yet implemented.
 */
int64_t sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
  if (!current_process || !req)
    return -EFAULT;
  if (validate_userspace_buffer((void *)req, sizeof(struct timespec)) != 0)
    return -EFAULT;
  if (rem && validate_userspace_buffer(rem, sizeof(struct timespec)) != 0)
    return -EFAULT;

  /* Convert to milliseconds; clamp tv_nsec to 0-999999999 */
  int64_t sec = req->tv_sec;
  long nsec = req->tv_nsec;
  if (sec < 0 || nsec < 0 || nsec > 999999999)
    return -EINVAL;

  uint64_t ms = (uint64_t)sec * 1000UL + (uint64_t)(nsec / 1000000);
  if (ms == 0)
    return 0;

  /* Find free slot */
  struct sleep_waiter *w = NULL;
  for (unsigned int i = 0; i < MAX_SLEEP_WAITERS; i++)
  {
    if (!sleep_waiters[i].proc)
    {
      w = &sleep_waiters[i];
      break;
    }
  }
  if (!w)
    return -EAGAIN;

  uint64_t now = clock_get_uptime_milliseconds();
  w->proc = current_process;
  w->wake_time_ms = now + ms;

  current_process->state = PROCESS_BLOCKED;
  sched_schedule_next();

  /* Woken by sleep_wake_check */
  w->proc = NULL;
  if (rem)
  {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

/*
 * rtc_time_to_unix - Convert RTC date/time to Unix timestamp (seconds since 1970-01-01 UTC).
 * Simplified: assumes UTC, no leap seconds.
 */
static time_t rtc_time_to_unix(const rtc_time_t *rt)
{
  uint16_t year = (rt->century > 0 && rt->century < 100) ?
      (rt->century * 100 + rt->year) : (2000 + rt->year);
  if (year < 1970)
    year = 1970;

  /* Days per month (non-leap) */
  static const int days_in_month[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
  int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 1 : 0;

  /* Days from 1970 to start of year */
  time_t days = 0;
  for (uint16_t y = 1970; y < year; y++)
    days += 365 + ((y % 4 == 0 && (y % 100 != 0 || y % 400 == 0)) ? 1 : 0);

  /* Days in current year before month */
  for (int m = 1; m < (int)rt->month && m <= 12; m++)
    days += days_in_month[m - 1] + (m == 2 ? leap : 0);

  days += (rt->day > 0 && rt->day <= 31) ? (rt->day - 1) : 0;

  return (time_t)days * 86400 +
      (rt->hour < 24 ? rt->hour : 0) * 3600 +
      (rt->minute < 60 ? rt->minute : 0) * 60 +
      (rt->second < 60 ? rt->second : 0);
}

/**
 * sys_gettimeofday - Get current time (POSIX, OSDev Time And Date)
 * @tv: Output timeval (seconds + microseconds since 1970-01-01 UTC)
 * @tz: Timezone (ignored, for compatibility)
 *
 * Uses RTC for wall-clock time. Falls back to uptime if RTC unavailable.
 */
int64_t sys_gettimeofday(struct timeval *tv, void *tz)
{
  (void)tz;
  if (!current_process || !tv)
    return -EFAULT;
  if (validate_userspace_buffer(tv, sizeof(struct timeval)) != 0)
    return -EFAULT;

  rtc_time_t rt;
  if (rtc_read_time(&rt) == 0)
  {
    tv->tv_sec = rtc_time_to_unix(&rt);
    tv->tv_usec = 0;  /* RTC has 1-second resolution */
    return 0;
  }

  /* Fallback: uptime since boot */
  uint64_t uptime_ms = clock_get_uptime_milliseconds();
  tv->tv_sec = (time_t)(uptime_ms / 1000);
  tv->tv_usec = (suseconds_t)((uptime_ms % 1000) * 1000);
  return 0;
}


/**
 * sys_ioctl - Device I/O control (POSIX ioctl)
 * @fd: File descriptor
 * @request: I/O control request code
 * @arg: Optional argument pointer
 * 
 * Returns: 0 on success, -1 on error
 */
int64_t sys_ioctl(int fd, uint64_t request, void *arg)
{
  if (!current_process)
    return -ESRCH;

  /* Handle device files (FD_DEV_BASE .. FD_SYS_BASE) before fd_table bounds check */
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    ensure_devfs_init();
    int device_id = fd - FD_DEV_BASE;
    devfs_node_t *node = devfs_find_node_by_id(device_id);
    
    if (!node || !node->ops || !node->ops->ioctl)
      return -ENOTTY; /* Not a TTY/device or ioctl not supported */

    /* Validate arg pointer if provided (most ioctls use arg) */
    if (arg && validate_userspace_buffer(arg, 256) != 0)
      return -EFAULT;

    /* Call device-specific ioctl handler */
    return node->ops->ioctl(&node->entry, request, arg);
  }

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_devfs)
  {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node_by_id(fd_table[fd].dev_device_id);

    if (!node || !node->ops || !node->ops->ioctl)
      return -ENOTTY;
    if (arg && validate_userspace_buffer(arg, 256) != 0)
      return -EFAULT;
    return node->ops->ioctl(&node->entry, request, arg);
  }

  return -ENOTTY;
}

int64_t sys_close(int fd)
{
  if (!current_process)
    return -ESRCH;
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node_by_id((uint32_t)(fd - FD_DEV_BASE));
    if (node)
      devfs_close_node(node);
    return 0;
  }

  if (pseudo_fs_find_by_fd(fd))
    return pseudo_fs_close_fd(fd);

  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
    return 0;  /* /proc */
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return 0;  /* /sys */

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  {
    int was_pipe = fd_table[fd].is_pipe ? 1 : 0;
    int was_devfs = fd_table[fd].is_devfs ? 1 : 0;

    if (fd <= 2 && !was_pipe && !was_devfs && !fd_table[fd].vfs_file)
      return -EBADF;

    if (was_devfs)
    {
      devfs_node_t *node = devfs_find_node_by_id(fd_table[fd].dev_device_id);

      if (node)
        devfs_close_node(node);
    }

    /* Check if this is a pipe */
    if (fd_table[fd].is_pipe && fd_table[fd].vfs_file)
    {
      pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;

      pipe_fase49_fd_trace((uint32_t)current_process->task.pid, fd, pipe,
			   fd_table[fd].pipe_end, pipe->fd_refs, "CLOSE");
      pipe_close_end(pipe, fd_table[fd].pipe_end);
      pipe_wake_all(pipe);
      fd_table[fd].vfs_file = NULL;
    }
    else if (fd_table[fd].vfs_file)
    {
      struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;

      vfs_close(vfs_file);
      fd_table[fd].vfs_file = NULL;
    }

    fd_table[fd].in_use = false;
    fd_table[fd].is_pipe = false;
    fd_table[fd].pipe_end = -1;
    fd_table[fd].is_devfs = false;
    fd_table[fd].dev_device_id = 0;
    fd_table[fd].path[0] = '\0';
    fd_table[fd].flags = 0;
    fd_table[fd].fd_flags = 0;
    fd_table[fd].offset = 0;
    fase48_note_fd_destroyed();
    fase51_dbg_close(fd, was_pipe, 0);
  }

  return 0;
}

int64_t process_close_fd(process_t *proc, int fd)
{
  process_t *saved = current_process;
  int64_t ret;

  if (!proc)
    return -ESRCH;

  current_process = proc;
  ret = sys_close(fd);
  current_process = saved;
  return ret;
}

int64_t sys_lseek(int fd, off_t offset, int whence)
{
  if (!current_process)
    return -ESRCH;

  if (fd >= FD_PROC_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return -ESPIPE;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd <= 2) {
    off_t new_offset;
    if (whence == SEEK_SET)
      new_offset = offset;
    else if (whence == SEEK_CUR)
      new_offset = (off_t)fd_table[fd].offset + offset;
    else
      return -ESPIPE;
    if (new_offset < 0)
      return -EINVAL;
    fd_table[fd].offset = (uint64_t)new_offset;
    fase52_dbg_lseek(fd, offset, whence, new_offset);
    return new_offset;
  }

  if (fd_table[fd].vfs_file) {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    off_t result = vfs_lseek(vfs_file, offset, whence);
    if (result < 0)
      return result;
    fd_table[fd].offset = (uint64_t)result;
    fase52_dbg_lseek(fd, offset, whence, result);
    return result;
  }

  /* No VFS handle: stat-based fallback */
  stat_t st;
  if (vfs_stat(fd_table[fd].path, &st) != 0)
    return -EBADF;

  off_t new_offset;
  switch (whence) {
  case SEEK_SET: new_offset = offset; break;
  case SEEK_CUR: new_offset = (off_t)fd_table[fd].offset + offset; break;
  case SEEK_END: new_offset = st.st_size + offset; break;
  default: return -EINVAL;
  }
  if (new_offset < 0)
    return -EINVAL;
  fd_table[fd].offset = (uint64_t)new_offset;
  fase52_dbg_lseek(fd, offset, whence, new_offset);
  return new_offset;
}

int64_t sys_dup2(int oldfd, int newfd)
{
  if (!current_process)
    return -ESRCH;

  if (oldfd < 0 || oldfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;
  if (newfd < 0 || newfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  if (oldfd == newfd)
    return newfd;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[oldfd].in_use)
    return -EBADF;

  if (fd_table[newfd].in_use && newfd != oldfd)
  {
    /*
     * Each fd slot owns at most one kernel object (pipe or vfs_file). POSIX
     * dup2 closes the previous occupant of newfd before reassigning; skipping
     * that leaks struct vfs_file and breaks refcounting.
     */
    if (fd_table[newfd].is_pipe && fd_table[newfd].vfs_file)
    {
      pipe_t *p = (pipe_t *)fd_table[newfd].vfs_file;

      pipe_close_end(p, fd_table[newfd].pipe_end);
      pipe_wake_all(p);
      fd_table[newfd].vfs_file = NULL;
    }
    else if (fd_table[newfd].is_devfs)
    {
      devfs_node_t *node = devfs_find_node_by_id(fd_table[newfd].dev_device_id);

      if (node)
        devfs_close_node(node);
    }
    else if (fd_table[newfd].vfs_file)
    {
      vfs_close((struct vfs_file *)fd_table[newfd].vfs_file);
      fd_table[newfd].vfs_file = NULL;
    }
    fd_table[newfd].in_use = false;
    fd_table[newfd].path[0] = '\0';
    fd_table[newfd].flags = 0;
    fd_table[newfd].fd_flags = 0;
    fd_table[newfd].offset = 0;
    fd_table[newfd].is_pipe = false;
    fd_table[newfd].pipe_end = -1;
    fd_table[newfd].is_devfs = false;
    fd_table[newfd].dev_device_id = 0;
    fase48_note_fd_destroyed();
  }

  fd_table[newfd].in_use = true;
  strncpy(fd_table[newfd].path, fd_table[oldfd].path, sizeof(fd_table[newfd].path) - 1);
  fd_table[newfd].path[sizeof(fd_table[newfd].path) - 1] = '\0';
  fd_table[newfd].flags = fd_table[oldfd].flags;
  fd_table[newfd].fd_flags = fd_table[oldfd].fd_flags;
  fd_table[newfd].offset = fd_table[oldfd].offset;
  fd_table[newfd].is_pipe = fd_table[oldfd].is_pipe;
  fd_table[newfd].pipe_end = fd_table[oldfd].pipe_end;
  fd_table[newfd].is_devfs = fd_table[oldfd].is_devfs;
  fd_table[newfd].dev_device_id = fd_table[oldfd].dev_device_id;

  /*
   * Regular files now share one open file description (vfs_file with refcount),
   * matching Unix dup/dup2 offset-sharing semantics.
   * Pipes share one pipe_t and keep per-end counts.
   */
  if (fd_table[oldfd].is_devfs)
  {
    devfs_node_t *node = devfs_find_node_by_id(fd_table[oldfd].dev_device_id);

    if (node)
      node->ref_count++;
  }
  else if (fd_table[oldfd].is_pipe)
  {
    pipe_acquire_end((pipe_t *)fd_table[oldfd].vfs_file, fd_table[oldfd].pipe_end);
    fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;
  }
  else if (fd_table[oldfd].vfs_file)
  {
    struct vfs_file *shared = (struct vfs_file *)fd_table[oldfd].vfs_file;

    vfs_file_acquire(shared);
    fd_table[newfd].vfs_file = shared;
  }
  else
  {
    fd_table[newfd].vfs_file = NULL;
  }

  if (fd_table[newfd].is_pipe && fd_table[newfd].vfs_file)
  {
    pipe_t *pip = (pipe_t *)fd_table[newfd].vfs_file;

    pipe_fase49_fd_trace((uint32_t)current_process->task.pid, newfd, pip,
			 fd_table[newfd].pipe_end, pip->fd_refs, "DUP2");
  }

  fase48_note_fd_created();
  fase51_dbg_dup2(oldfd, newfd, newfd);
  return newfd;
}

/* Helper function to get file stats by path (for ls improvement) */

/*
 * sys_fork — Linux __NR_fork; duplicates address space via fork() in process.c.
 */
int64_t sys_fork(void)
{
  int64_t r;
  if (!current_process)
    return -ESRCH;

  r = fork();
  return r;
}

/*
 * sys_clone — Linux __NR_clone (56). Thread flags (CLONE_THREAD) are unsupported;
 * otherwise duplicates the address space like fork().
 */
int64_t sys_clone(unsigned long flags, void *stack, int *parent_tid,
                  int *child_tid, unsigned long tls)
{
  (void)stack;
  (void)parent_tid;
  (void)child_tid;
  (void)tls;

  if (!current_process)
    return -ESRCH;

  /* CLONE_THREAD and other pthread paths need a real thread model */
  if (flags & 0x00010000UL)
    return -ENOSYS;

  return fork();
}


int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  serial_print("[WAIT_EXIT_AUDIT][sys_wait4] entry parent_pid=");
  serial_print_hex32((uint32_t)current_process->task.pid);
  serial_print(" wait_pid=");
  serial_print_hex32((uint32_t)pid);
  serial_print(" status_ptr=");
  serial_print_hex64((uint64_t)(uintptr_t)status);
  serial_print(" options=");
  serial_print_hex64((uint64_t)(unsigned int)options);
  serial_print("\n");

  fase50_trace_syscall_proc("sys_wait4-entry", current_process);
  process_fase46_note_wait(current_process);
  {
    int64_t ret = process_wait(pid, status, options);

    serial_print("[WAIT_EXIT_AUDIT][sys_wait4] return parent_pid=");
    serial_print_hex32((uint32_t)current_process->task.pid);
    serial_print(" ret=");
    serial_print_hex64((uint64_t)ret);
    serial_print("\n");
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
    oldact->sa_mask = current_process->signal_mask;
    oldact->sa_flags = 0; /* Flags not yet implemented */
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

    /* Update signal mask (blocked signals during handler execution) */
    current_process->signal_mask = act->sa_mask;
  }

  return 0;
}

/*
 * sys_rt_sigprocmask - Linux rt_sigprocmask(2) subset for musl startup.
 */
int64_t sys_rt_sigprocmask(int how, const uint64_t *set, uint64_t *oldset, size_t sigsetsize)
{
  uint64_t newmask;

  if (!current_process)
    return -ESRCH;

  if (sigsetsize != sizeof(uint64_t))
    return -EINVAL;

  if (oldset)
  {
    if (validate_userspace_buffer(oldset, sizeof(uint64_t)) != 0)
      return -EFAULT;
    newmask = (uint64_t)current_process->signal_mask;
    if (copy_to_user(oldset, &newmask, sizeof(uint64_t)) != 0)
      return -EFAULT;
  }

  if (!set)
    return 0;

  if (validate_userspace_buffer(set, sizeof(uint64_t)) != 0)
    return -EFAULT;

  if (copy_from_user(&newmask, set, sizeof(uint64_t)) != 0)
    return -EFAULT;

  switch (how)
  {
  case 0: /* SIG_BLOCK */
    current_process->signal_mask |= (uint32_t)newmask;
    break;
  case 1: /* SIG_UNBLOCK */
    current_process->signal_mask &= ~(uint32_t)newmask;
    break;
  case 2: /* SIG_SETMASK */
    current_process->signal_mask = (uint32_t)newmask;
    break;
  default:
    return -EINVAL;
  }

  return 0;
}

/*
 * sys_arch_prctl - x86-64 arch_prctl(2) subset (ARCH_SET_FS / ARCH_GET_FS).
 */
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
    fsbase = arch_get_fs_base();
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

/*
 * sys_fcntl - Minimal fcntl(2) for musl and libc startup.
 */
int64_t sys_fcntl(int fd, int cmd, unsigned long arg)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  switch (cmd)
  {
  case F_GETFD:
    return (fd_table[fd].fd_flags & FD_CLOEXEC) ? FD_CLOEXEC : 0;
  case F_SETFD:
    fd_table[fd].fd_flags = (uint8_t)(arg & FD_CLOEXEC);
    return 0;
  case F_GETFL:
    return fd_table[fd].flags;
  case F_SETFL:
    fd_table[fd].flags = (int)arg;
    return 0;
  case F_DUPFD:
    return sys_dup2(fd, (int)arg);
  default:
    return -EINVAL;
  }
}


#define FUTEX_WAIT 0
#define FUTEX_WAKE 1

struct robust_list_head
{
  void *list;
  void *list_op_pending;
  void *list_op_next;
};


/*
 * sys_clock_gettime - POSIX clock_gettime(2) subset.
 */
int64_t sys_clock_gettime(int clock_id, struct timespec *tp)
{
  uint64_t uptime_ms;

  if (!current_process || !tp)
    return -EFAULT;

  if (validate_userspace_buffer(tp, sizeof(struct timespec)) != 0)
    return -EFAULT;

  if (clock_id != 0 && clock_id != 1)
    return -EINVAL;

  if (clock_id == 1)
  {
    uptime_ms = clock_get_uptime_milliseconds();
    tp->tv_sec = (time_t)(uptime_ms / 1000);
    tp->tv_nsec = (long)((uptime_ms % 1000) * 1000000UL);
    return 0;
  }

  {
    struct timeval tv;

    if (sys_gettimeofday(&tv, NULL) != 0)
      return -EIO;

    tp->tv_sec = tv.tv_sec;
    tp->tv_nsec = (long)(tv.tv_usec * 1000);
  }

  return 0;
}

/*
 * sys_futex - Minimal futex for musl pthread bring-up.
 */
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

/*
 * sys_set_robust_list - Store robust mutex list head pointer.
 */
int64_t sys_set_robust_list(struct robust_list_head *head, size_t len)
{
  (void)head;
  (void)len;

  if (!current_process)
    return -ESRCH;

  return 0;
}

/*
 * sys_prlimit64 - Return generous default rlimits.
 */
int64_t sys_prlimit64(pid_t pid, unsigned int resource, const void *new_limit,
                      void *old_limit)
{
  (void)new_limit;

  if (!current_process)
    return -ESRCH;

  if (pid != 0 && pid != (pid_t)current_process->task.pid)
    return -ESRCH;

  if (resource > 15)
    return -EINVAL;

  if (old_limit)
  {
    uint64_t lim[2] = { 0, (uint64_t)-1 };

    if (validate_userspace_buffer(old_limit, 16) != 0)
      return -EFAULT;
    if (copy_to_user(old_limit, lim, 16) != 0)
      return -EFAULT;
  }

  return 0;
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

/**
 * sys_pipe - Create a pipe (POSIX pipe syscall)
 * @pipefd: Array of 2 file descriptors [read_fd, write_fd] (output)
 *
 * Returns: 0 on success, -1 on error
 */
int64_t sys_pipe(int pipefd[2])
{
  return sys_pipe2(pipefd, 0);
}

static int64_t sys_pipe_install(int pipefd[2], int flags)
{
  pipe_t *pipe;
  fd_entry_t *fd_table;
  int read_fd = -1;
  int write_fd = -1;
  uint8_t cloexec = 0;
  int read_fl = O_RDONLY;
  int write_fl = O_WRONLY;

  if (!current_process)
    return -ESRCH;

  if (!pipefd)
    return -EFAULT;

  if (validate_userspace_buffer(pipefd, sizeof(int) * 2) != 0)
    return -EFAULT;

  if (flags & ~(O_NONBLOCK | O_CLOEXEC))
    return -EINVAL;

  if (flags & O_NONBLOCK)
  {
    read_fl |= O_NONBLOCK;
    write_fl |= O_NONBLOCK;
  }
  if (flags & O_CLOEXEC)
    cloexec = FD_CLOEXEC;

  if (current_process->task.pid == 1)
    process_fase48_capture_fd_baseline(current_process);

  pipe = pipe_create();
  if (!pipe)
    return -ENOMEM;

  fd_table = get_process_fd_table();
  for (int i = 3; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      if (read_fd == -1)
        read_fd = i;
      else if (write_fd == -1)
      {
        write_fd = i;
        break;
      }
    }
  }

  if (read_fd == -1 || write_fd == -1)
  {
    pipe_abort_unopened(pipe);
    return -EMFILE;
  }

  fd_table[read_fd].in_use = true;
  strncpy(fd_table[read_fd].path, "/dev/pipe", sizeof(fd_table[read_fd].path) - 1);
  fd_table[read_fd].path[sizeof(fd_table[read_fd].path) - 1] = '\0';
  fd_table[read_fd].offset = 0;
  fd_table[read_fd].flags = read_fl;
  fd_table[read_fd].fd_flags = cloexec;
  fd_table[read_fd].vfs_file = (void *)pipe;
  fd_table[read_fd].is_pipe = true;
  fd_table[read_fd].pipe_end = 0;

  fd_table[write_fd].in_use = true;
  strncpy(fd_table[write_fd].path, "/dev/pipe", sizeof(fd_table[write_fd].path) - 1);
  fd_table[write_fd].path[sizeof(fd_table[write_fd].path) - 1] = '\0';
  fd_table[write_fd].offset = 0;
  fd_table[write_fd].flags = write_fl;
  fd_table[write_fd].fd_flags = cloexec;
  fd_table[write_fd].vfs_file = (void *)pipe;
  fd_table[write_fd].is_pipe = true;
  fd_table[write_fd].pipe_end = 1;

  pipe_acquire_end(pipe, 0);
  pipe_acquire_end(pipe, 1);

  fase48_note_fd_created();
  fase48_note_fd_created();

  {
    int kfd[2];

    kfd[0] = read_fd;
    kfd[1] = write_fd;
    if (copy_to_user(pipefd, kfd, sizeof(kfd)) != 0)
    {
      pipe_t *pip = pipe;

      fd_table[read_fd].in_use = false;
      fd_table[read_fd].vfs_file = NULL;
      fd_table[read_fd].is_pipe = false;
      fd_table[read_fd].pipe_end = -1;
      fd_table[write_fd].in_use = false;
      fd_table[write_fd].vfs_file = NULL;
      fd_table[write_fd].is_pipe = false;
      fd_table[write_fd].pipe_end = -1;
      pipe_close_end(pip, 0);
      pipe_close_end(pip, 1);
      fase48_note_fd_destroyed();
      fase48_note_fd_destroyed();
      return -EFAULT;
    }
  }

  fase51_dbg_pipe2(read_fd, write_fd, flags, 0);
  return 0;
}

int64_t sys_pipe2(int pipefd[2], int flags)
{
  return sys_pipe_install(pipefd, flags);
}

int64_t sys_brk(void *addr)
{
  if (!current_process)
    return -ESRCH;

  /* If addr is NULL, return current break */
  if (!addr)
    return (int64_t)current_process->heap_end;

  /* Validate new break address is in userspace */
  if (!is_user_address(addr, 0))
    return -EFAULT;

  uintptr_t new_brk = (uintptr_t)addr;
  uintptr_t current_brk = current_process->heap_end;
  
  /* Initialize heap_start if not set */
  if (current_process->heap_start == 0)
  {
    /* Start heap at USER_HEAP_BASE - after code/stack */
    current_process->heap_start = USER_HEAP_BASE;
    current_process->heap_end = current_process->heap_start;
    current_brk = current_process->heap_end;
  }

  /* Validate new break is within reasonable range */
  if (new_brk < current_process->heap_start ||
      new_brk > (current_process->heap_start + USER_HEAP_MAX_SIZE))
    return -EFAULT;

  /* If expanding heap, map new pages */
  if (new_brk > current_brk)
  {
    /* Align to page boundary */
    uintptr_t start_page = (current_brk + 0xFFF) & ~0xFFF;
    uintptr_t end_page = (new_brk + 0xFFF) & ~0xFFF;
    size_t size_to_map = end_page - start_page;
    
    if (size_to_map > 0)
    {
      /* Map new heap pages in process page directory */
      if (map_user_region_in_directory(current_process->page_directory, start_page, size_to_map, PAGE_RW) != 0)
      {
        /* Failed to map - return current break */
        return (int64_t)current_process->heap_end;
      }
    }
  }
  /* If shrinking heap, unmap pages */
  else if (new_brk < current_brk)
  {
    uintptr_t old_end = current_brk;

    /*
     * Unmap fully abandoned pages: first page strictly above new_brk up to
     * (but not including) the page containing old_end when old_end is aligned.
     */
    for (uintptr_t page = (new_brk + (PAGE_SIZE_4KB - 1)) & (uintptr_t)PAGE_FRAME_MASK;
         page < old_end;
         page += PAGE_SIZE_4KB)
    {
      unmap_page_in_directory(current_process->page_directory, page);
    }
  }

  /* Set new break */
  current_process->heap_end = new_brk;
  fase39_dump_current_vmas("brk");
  fase52_dbg_brk(addr, (int64_t)new_brk);
  return (int64_t)new_brk;
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */


/* mmap flags */
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_SHARED 0x01
#define MAP_FIXED_AUDIT 0x10

/* Protection flags */
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define PROT_NONE 0x0
#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))
#define MMAP_AUDIT_FAILED ((void *)(intptr_t)-1)

static int mmap_audit_ptr_err(void *ret)
{
  return ((intptr_t)ret < 0);
}

static int mmap_audit_errno_from_ret(void *ret)
{
  if (!mmap_audit_ptr_err(ret))
    return 0;
  return -(int)(intptr_t)ret;
}

static void mmap_audit_log_pte(const char *tag, uint64_t *pml4, uintptr_t va)
{
  uint64_t pte_flags = 0;
  uint64_t *pte;
  int mapped;

  if (!pml4)
    return;

  mapped = is_page_mapped_in_directory(pml4, va, &pte_flags);
  pte = paging_get_pte(pml4, va);

  serial_print("[MMAP_AUDIT][PTE] tag=");
  serial_print(tag ? tag : "(null)");
  serial_print(" va=");
  serial_print_hex64((uint64_t)va);
  serial_print(" mapped=");
  serial_print_hex64((uint64_t)(mapped > 0 ? 1 : 0));
  serial_print(" present=");
  serial_print_hex64((uint64_t)(pte && (*pte & PAGE_PRESENT) ? 1 : 0));
  serial_print(" user=");
  serial_print_hex64((uint64_t)(pte_flags & PAGE_USER ? 1 : 0));
  serial_print(" rw=");
  serial_print_hex64((uint64_t)(pte_flags & PAGE_RW ? 1 : 0));
  serial_print(" nx=");
  serial_print_hex64((uint64_t)(pte && (*pte & PAGE_NX) ? 1 : 0));
  if (pte && (*pte & PAGE_PRESENT))
  {
    serial_print(" pfn=");
    serial_print_hex64((uint64_t)(*pte & PAGE_PTE_PFN_MASK));
  }
  serial_print("\n");
}

static void mmap_audit_log_args(void *addr, size_t length, int prot, int flags,
                                int fd, off_t offset)
{
  serial_print("[MMAP_AUDIT][ARGS] classify=MMAP_ARGS_DECODED pid=");
  serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
  serial_print(" comm=");
  serial_print(current_process ? current_process->comm : "(none)");
  serial_print(" addr=");
  serial_print_hex64((uint64_t)(uintptr_t)addr);
  serial_print(" length=");
  serial_print_hex64((uint64_t)length);
  serial_print(" prot=");
  serial_print_hex64((uint64_t)(unsigned int)prot);
  serial_print(" flags=");
  serial_print_hex64((uint64_t)(unsigned int)flags);
  serial_print(" fd=");
  serial_print_hex64((uint64_t)(unsigned int)fd);
  serial_print(" offset=");
  serial_print_hex64((uint64_t)offset);
  if (current_process)
  {
    serial_print(" caller_rip=");
    serial_print_hex64(current_process->syscall_frame.rip);
    serial_print(" caller_rsp=");
    serial_print_hex64(current_process->syscall_frame.rsp);
  }
  serial_print("\n");

  if ((flags & MAP_FIXED_AUDIT) != 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_UNSUPPORTED_FLAGS reason=MAP_FIXED_not_implemented\n");
  }
  if ((flags & MAP_SHARED) != 0 && (flags & MAP_PRIVATE) != 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_UNSUPPORTED_FLAGS reason=MAP_SHARED_and_MAP_PRIVATE\n");
  }
  if (!(flags & MAP_ANONYMOUS) && fd < 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_UNSUPPORTED_FLAGS reason=file_map_without_fd\n");
  }
}

static void mmap_audit_log_return(const char *stage, void *ret, uintptr_t virt_addr,
                                  size_t length, int vma_inserted, uint64_t *pml4)
{
  size_t pages = (length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
  size_t mapped_pages = 0;
  size_t i;

  serial_print("[MMAP_AUDIT][RET] stage=");
  serial_print(stage ? stage : "(null)");
  serial_print(" ret=");
  serial_print_hex64((uint64_t)(uintptr_t)ret);
  if (mmap_audit_ptr_err(ret))
  {
    serial_print(" errno=");
    serial_print_hex64((uint64_t)(unsigned int)mmap_audit_errno_from_ret(ret));
  }
  else
  {
    serial_print(" range=[");
    serial_print_hex64((uint64_t)virt_addr);
    serial_print(",");
    serial_print_hex64((uint64_t)(virt_addr + length));
    serial_print(") pages=");
    serial_print_hex64((uint64_t)pages);
  }
  serial_print(" vma_inserted=");
  serial_print_hex64((uint64_t)(unsigned int)vma_inserted);
  serial_print("\n");

  if (mmap_audit_ptr_err(ret) || !pml4 || virt_addr == 0 || length == 0)
    return;

  for (i = 0; i < pages; i++)
  {
    uintptr_t va = virt_addr + i * PAGE_SIZE_4KB;
    if (is_page_mapped_in_directory(pml4, va, NULL) == 1)
      mapped_pages++;
  }

  serial_print("[MMAP_AUDIT][RET] pte_mapped_pages=");
  serial_print_hex64((uint64_t)mapped_pages);
  serial_print(" pte_expected_pages=");
  serial_print_hex64((uint64_t)pages);
  serial_print("\n");

  if (mapped_pages == 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_VMA_WITHOUT_PTES\n");
  }
  else if (mapped_pages < pages)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_RET_UNMAPPED_RANGE\n");
  }

  mmap_audit_log_pte("first", pml4, virt_addr);
  if (pages > 1)
    mmap_audit_log_pte("last", pml4, virt_addr + (pages - 1) * PAGE_SIZE_4KB);

  if (pml4)
  {
    uint64_t flags_low = 0;

    if (is_page_mapped_in_directory(pml4, virt_addr, &flags_low) == 1)
    {
      if (!(flags_low & PAGE_USER))
      {
        serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_USER\n");
      }
      if (!(flags_low & PAGE_RW))
      {
        serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_RW\n");
      }
    }
  }
}

/*
 * FASE39 diagnostics: dump current process VMAs after brk/mmap/munmap.
 * Pure observability helper (no policy changes).
 */
static void fase39_dump_current_vmas(const char *tag)
{
  struct mmap_region *r;

  if (!current_process)
    return;

  serial_print("[FASE39][VMA] tag=");
  serial_print(tag ? tag : "(null)");
  serial_print(" pid=");
  serial_print_hex32((uint32_t)current_process->task.pid);
  serial_print(" comm=");
  serial_print(current_process->comm);
  serial_print("\n");

  serial_print("[FASE39][VMA] heap [");
  serial_print_hex64(current_process->heap_start);
  serial_print(",");
  serial_print_hex64(current_process->heap_end);
  serial_print(") backing=anon\n");

  serial_print("[FASE39][VMA] stack [");
  serial_print_hex64(current_process->stack_start);
  serial_print(",");
  serial_print_hex64(current_process->stack_start + current_process->stack_size);
  serial_print(") backing=stack\n");

  for (r = current_process->mmap_list; r; r = r->next)
  {
    serial_print("[FASE39][VMA] mmap [");
    serial_print_hex64((uint64_t)(uintptr_t)r->addr);
    serial_print(",");
    serial_print_hex64((uint64_t)(uintptr_t)r->addr + (uint64_t)r->length);
    serial_print(") perm=");
    serial_print((r->prot & PROT_READ) ? "r" : "-");
    serial_print((r->prot & PROT_WRITE) ? "w" : "-");
    serial_print((r->prot & PROT_EXEC) ? "x" : "-");
    serial_print(" type=");
    serial_print((r->flags & MAP_ANONYMOUS) ? "anon" : "file");
    serial_print(" backing=");
    if ((r->flags & MAP_ANONYMOUS) != 0)
      serial_print("anonymous");
    else
      serial_print("fd-backed-or-device");
    serial_print("\n");
  }
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *ret;
  uintptr_t virt_addr_out = 0;
  size_t aligned_len = 0;
  int vma_inserted = 0;

  serial_print("SERIAL: mmap: entering syscall\n");
  mmap_audit_log_args(addr, length, prot, flags, fd, offset);

  if (!current_process)
  {
    serial_print("SERIAL: mmap: no current process\n");
    ret = (void *)(intptr_t)-ESRCH;
    mmap_audit_log_return("no-process", ret, 0, 0, 0, NULL);
    return ret;
  }

  if (length == 0)
  {
    serial_print("SERIAL: mmap: zero length\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("zero-length", ret, 0, 0, 0, current_process->page_directory);
    return ret;
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    serial_print("SERIAL: mmap: invalid protection flags\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("bad-prot", ret, 0, 0, 0, current_process->page_directory);
    return ret;
  }

  /* Validate offset alignment for file mappings */
  if (!(flags & MAP_ANONYMOUS))
  {
    if (fd < 0)
    {
      serial_print("SERIAL: mmap: file mapping requires valid fd\n");
      return SYSCALL_PTR_ERR(EBADF);
    }
    
    /* Offset must be page-aligned for file mappings */
    if (offset % PAGE_SIZE_4KB != 0)
    {
      serial_print("SERIAL: mmap: offset must be page-aligned\n");
      return SYSCALL_PTR_ERR(EINVAL);
    }
    
    /*
     * mmap of /dev/fb0 — legacy virtual fd (FD_DEV_BASE + 15) or real devfs fd
     * (FASE58A: fd_table slot with is_devfs + dev_device_id 15).
     * Maps framebuffer physical memory into userspace for efficient access.
     */
    {
      bool fb_mmap_legacy = false;
      bool fb_mmap_devfs = false;
      uint32_t device_id = UINT32_MAX;
      fd_entry_t *fd_table = get_process_fd_table();

      if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
      {
        device_id = (uint32_t)(fd - FD_DEV_BASE);
        fb_mmap_legacy = true;
      }
      else if (fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
               fd_table && fd_table[fd].in_use && fd_table[fd].is_devfs)
      {
        device_id = fd_table[fd].dev_device_id;
        fb_mmap_devfs = true;
      }

#if CONFIG_ENABLE_VBE
      if (device_id == 15U)
      {
        struct ir0_fb_info fb_info;
        uint32_t fb_phys;
        uint32_t fb_size;

        if (!ir0_fb_get_info(&fb_info))
          return SYSCALL_PTR_ERR(ENODEV);

        fb_phys = fb_info.fb_phys;
        fb_size = fb_info.fb_size;
        if (fb_phys == 0 || fb_size == 0)
          return SYSCALL_PTR_ERR(ENODEV);

        if (offset < 0 || (uint64_t)offset >= (uint64_t)fb_size)
          return SYSCALL_PTR_ERR(EINVAL);

        uint64_t off_u = (uint64_t)offset;
        uint64_t rem = (uint64_t)fb_size - off_u;
        size_t map_len = length;

        if ((uint64_t)map_len > rem)
          map_len = (size_t)rem;
        map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
        if (map_len == 0)
          return SYSCALL_PTR_ERR(EINVAL);

        uintptr_t virt_addr = 0;
        if (addr != NULL)
        {
          uintptr_t hint_addr = (uintptr_t)addr;
          if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
            return SYSCALL_PTR_ERR(EINVAL);
          if (!is_user_address(addr, map_len))
            return SYSCALL_PTR_ERR(EFAULT);
          int mapped = is_page_mapped_in_directory(current_process->page_directory, hint_addr, NULL);
          if (mapped == 1)
            return SYSCALL_PTR_ERR(EINVAL);
          virt_addr = hint_addr;
        }
        else
        {
          uintptr_t search_start = USER_MMAP_START;
          uintptr_t search_end = USER_MMAP_END;
          uintptr_t candidate = search_start;
          bool found = false;
          while (candidate + map_len < search_end && !found)
          {
            bool all_unmapped = true;
            for (uintptr_t check = candidate; check < candidate + map_len; check += PAGE_SIZE_4KB)
            {
              int m = is_page_mapped_in_directory(current_process->page_directory, check, NULL);
              if (m == 1)
              {
                all_unmapped = false;
                candidate = ((check + PAGE_SIZE_4KB) + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
                break;
              }
            }
            if (all_unmapped)
            {
              virt_addr = candidate;
              found = true;
            }
          }
          if (!found)
            return SYSCALL_PTR_ERR(ENOMEM);
        }
        
        uint64_t page_flags = PAGE_USER | PAGE_RW;
        for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
        {
          uintptr_t v = virt_addr + off;
          uintptr_t p = (uintptr_t)fb_phys + off_u + off;
          if (map_page_in_directory(current_process->page_directory, v, p, page_flags) != 0)
          {
            /* Rollback: unmap already mapped pages */
            for (size_t r = 0; r < off; r += PAGE_SIZE_4KB)
              unmap_page_in_directory(current_process->page_directory, virt_addr + r);
            return SYSCALL_PTR_ERR(ENOMEM);
          }
        }
        
        struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
        if (!region)
        {
          for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
            unmap_page_in_directory(current_process->page_directory, virt_addr + off);
          return SYSCALL_PTR_ERR(ENOMEM);
        }
        region->addr = (void *)virt_addr;
        region->hint_addr = addr;
        region->length = map_len;
        region->prot = prot;
        region->flags = flags;
        region->next = current_process->mmap_list;
        current_process->mmap_list = region;
        fase39_dump_current_vmas("mmap-fb");
        {
          static int s_fb_mmap_legacy_tag;
          static int s_fb_mmap_devfs_tag;
          static int s_fb_mmap_ok_tag;

          if (fb_mmap_legacy && !s_fb_mmap_legacy_tag)
          {
            s_fb_mmap_legacy_tag = 1;
            serial_print("FB_MMAP_LEGACY_FD_OK\n");
          }
          if (fb_mmap_devfs && !s_fb_mmap_devfs_tag)
          {
            s_fb_mmap_devfs_tag = 1;
            serial_print("FB_MMAP_DEVFS_FD_OK\n");
            serial_print("DEVFB0_MMAP_REAL_FD_OK\n");
          }
          if (!s_fb_mmap_ok_tag)
          {
            s_fb_mmap_ok_tag = 1;
            serial_print("FASE58C_FBDEV_MMAP_OK\n");
          }
        }
        return (void *)virt_addr;
      }
#else
      (void)fb_mmap_legacy;
      (void)fb_mmap_devfs;
      (void)device_id;
#endif
    }
    
    /* File-based mapping not yet implemented for other files */
    serial_print("SERIAL: mmap: file-based mapping not yet implemented\n");
    return SYSCALL_PTR_ERR(ENOSYS);
  }

  /* Align length to page boundary */
  length = (length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
  aligned_len = length;

  /* Address hint support:
   * - If addr is NULL: Kernel chooses address
   * - If addr is provided: Try to use it if valid and page-aligned
   * - If MAP_FIXED is set (future): Must use exact address
   */
  uintptr_t virt_addr = 0;
  uintptr_t hint_addr = (uintptr_t)addr;
  
  /* Check if hint address is provided and valid */
  if (addr != NULL)
  {
    /* Address must be page-aligned */
    if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
    {
      serial_print("SERIAL: mmap: address hint not page-aligned\n");
      return SYSCALL_PTR_ERR(EINVAL);
    }
    
    /* Check if hint is in valid userspace range */
    if (!is_user_address(addr, length))
    {
      serial_print("SERIAL: mmap: address hint not in userspace\n");
      return SYSCALL_PTR_ERR(EFAULT);
    }
    
    /* Check if address range is already mapped */
    int mapped = is_page_mapped_in_directory(current_process->page_directory, hint_addr, NULL);
    if (mapped == 1)
    {
      serial_print("SERIAL: mmap: address range already mapped\n");
      return SYSCALL_PTR_ERR(EINVAL);  /* Address already in use */
    }
    
    virt_addr = hint_addr;
  }
  else
  {
    /* Kernel chooses address - find unused address range */
    /* Start searching at USER_MMAP_START (after typical heap) */
    virt_addr = USER_MMAP_START;
    
    /* Find an unused address range of 'length' bytes */
    uintptr_t search_start = virt_addr;
    uintptr_t search_end = USER_MMAP_END;
    uintptr_t candidate = search_start;
    bool found = false;
    
    /* Search for unused region (simple linear search) */
    while (candidate + length < search_end && !found)
    {
      /* Check if all pages in this range are unmapped */
      bool all_unmapped = true;
      for (uintptr_t check = candidate; check < candidate + length; check += PAGE_SIZE_4KB)
      {
        int mapped = is_page_mapped_in_directory(current_process->page_directory, check, NULL);
        if (mapped == 1)
        {
          all_unmapped = false;
          /* Skip to next page-aligned address after this mapped page */
          candidate = ((check + PAGE_SIZE_4KB) + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
          break;
        }
      }
      
      if (all_unmapped)
      {
        virt_addr = candidate;
        found = true;
        break;
      }
    }
    
    /* If we couldn't find space, return error */
    if (!found)
    {
      return SYSCALL_PTR_ERR(ENOMEM);
    }
  }

  /* Determine page flags from protection flags */
  uint64_t page_flags = PAGE_USER;  /* Always user mode */
  if (prot & PROT_READ)
    page_flags |= 0;  /* Read is default */
  if (prot & PROT_WRITE)
    page_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    page_flags |= PAGE_EXEC;

  /* Map pages in process page directory */
  if (map_user_region_in_directory(current_process->page_directory, virt_addr, length, page_flags) != 0)
  {
    serial_print("SERIAL: mmap: failed to map pages\n");
    ret = SYSCALL_PTR_ERR(ENOMEM);
    mmap_audit_log_return("map-failed", ret, virt_addr, aligned_len, 0,
                          current_process->page_directory);
    return ret;
  }

  virt_addr_out = virt_addr;
  mmap_audit_log_pte("post-map", current_process->page_directory, virt_addr);

  /*
   * Anonymous zero-fill: map_user_region_in_directory() clears each
   * physical frame via the identity map before installing final PTEs.
   * Do not memset() through the user VA while PTEs lack PAGE_RW — that
   * faults in kernel mode on PROT_NONE / read-only mappings.
   */
  if (prot == PROT_NONE)
  {
    serial_print("[MMAP_AUDIT][ZERO] stage=skipped reason=prot_none\n");
  }
  else if (prot & PROT_WRITE)
  {
    serial_print("[MMAP_AUDIT][ZERO] stage=phys-prezeroed-in-map_user_region\n");
  }
  else
  {
    serial_print("[MMAP_AUDIT][ZERO] stage=skipped reason=no_prot_write\n");
  }

  /* Create mapping entry */
  struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
  if (!region)
  {
      /* Failed to allocate region entry - unmap pages */
      for (uintptr_t page = virt_addr; page < virt_addr + length; page += PAGE_SIZE_4KB)
    {
      unmap_page_in_directory(current_process->page_directory, page);
    }
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  region->addr = (void *)virt_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = current_process->mmap_list;
  current_process->mmap_list = region;
  vma_inserted = 1;
  fase39_dump_current_vmas("mmap");

  ret = (void *)virt_addr;
  mmap_audit_log_return("ok", ret, virt_addr_out, aligned_len, vma_inserted,
                        current_process->page_directory);
  serial_print("[MMAP_AUDIT][CLASSIFY] BUSYBOX_NEXT_SYSCALL_REACHED stage=mmap-return-ok\n");
  fase52_dbg_mmap(addr, length, prot, flags, fd, offset, ret);
  return ret;
}

int sys_munmap(void *addr, size_t length)
{
  uint64_t unmapped_pages = 0;
  paging_fase42_checkpoint("munmap-before", (int32_t)process_get_pid());
  if (!current_process)
    return -ESRCH;
  if (!addr || length == 0)
    return -EINVAL;

  /* Validate address is in userspace */
  if (!is_user_address(addr, length))
    return -EFAULT;

  /* Align to page boundaries */
  uintptr_t start_page = (uintptr_t)addr & ~0xFFF;
  size_t aligned_length = ((length + 0xFFF) & ~0xFFF);

  /* Find the mapping */
  struct mmap_region *current = current_process->mmap_list;
  struct mmap_region *prev = NULL;

  while (current)
  {
    uintptr_t mapping_start = (uintptr_t)current->addr & ~0xFFF;
    uintptr_t mapping_end = mapping_start + ((current->length + 0xFFF) & ~0xFFF);
    
    if (start_page >= mapping_start && (start_page + aligned_length) <= mapping_end)
    {
      /* Remove from list */
      if (prev)
        prev->next = current->next;
      else
        current_process->mmap_list = current->next;

      /* Unmap pages in process page directory */
      for (uintptr_t page = start_page; page < start_page + aligned_length; page += PAGE_SIZE_4KB)
      {
        if (unmap_page_in_directory(current_process->page_directory, page) == 0)
          unmapped_pages++;
      }

      /* Free the mapping structure */
      kfree(current);
      serial_print("[FASE41][MUNMAP] pid=");
      serial_print_hex32((uint32_t)current_process->task.pid);
      serial_print(" start=");
      serial_print_hex64((uint64_t)start_page);
      serial_print(" len=");
      serial_print_hex64((uint64_t)aligned_length);
      serial_print(" unmapped_pages=");
      serial_print_hex64(unmapped_pages);
      serial_print("\n");
      paging_fase42_checkpoint("munmap-after", (int32_t)current_process->task.pid);
      fase39_dump_current_vmas("munmap");
      fase52_dbg_munmap(addr, length, 0);
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -EINVAL; /* Not found */
}

int sys_mprotect(void *addr, size_t len, int prot)
{
  struct mmap_region *current;
  uintptr_t range_start;
  uintptr_t range_end;
  uint64_t *pml4;
  uint64_t map_flags;

  if (!current_process)
    return -ESRCH;
  if (!addr || len == 0)
    return -EINVAL;

  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return -EINVAL;

  if (!is_user_address(addr, len))
    return -EFAULT;

  /* Find the mapping */
  current = current_process->mmap_list;
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
      current->prot = prot;

      range_start = (uintptr_t)addr & (uintptr_t)PAGE_FRAME_MASK;
      range_end = (((uintptr_t)addr + len) + PAGE_SIZE_4KB - 1) & (uintptr_t)PAGE_FRAME_MASK;
      pml4 = current_process->page_directory;

      /*
       * map_page_in_directory() sets PTE present bit and applies PAGE_NX
       * when PAGE_EXEC is absent (matches PAGE_NX if !(prot & PROT_EXEC)).
       */
      map_flags = PAGE_USER;
      if (prot & PROT_WRITE)
        map_flags |= PAGE_RW;
      if (prot & PROT_EXEC)
        map_flags |= PAGE_EXEC;

      for (uintptr_t page = range_start; page < range_end; page += PAGE_SIZE_4KB)
      {
        uint64_t *pte;
        uint64_t phys;

        pte = paging_get_pte(pml4, page);
        if (!pte || !(*pte & PAGE_PRESENT))
          continue;

        phys = *pte & PAGE_FRAME_MASK;
        if (map_page_in_directory(pml4, page, phys, map_flags) != 0)
          return -ENOMEM;
      }

      fase52_dbg_mprotect(addr, len, prot, 0);
      return 0;
    }
    current = current->next;
  }

  fase52_dbg_mprotect(addr, len, prot, -EINVAL);
  return -EINVAL; /* Not found */
}

/* ========================================================================== */
/* DIRECTORY OPERATIONS                                                       */
/* ========================================================================== */

int64_t sys_chdir(const char *pathname)
{
  int64_t ret;

  if (!current_process || !pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Validate path length */
  size_t len = strlen(pathname);
  if (len == 0 || len >= 256)
    return -EINVAL;

  /* Calculate new path */
  char new_path[256];

  if (is_absolute_path(pathname)) {
    /* Absolute path - just normalize it */
    if (normalize_path(pathname, new_path, sizeof(new_path)) != 0)
      return -ENAMETOOLONG;
  } else {
    /* Relative path - join with current working directory */
    if (join_paths(current_process->cwd, pathname, new_path, sizeof(new_path)) != 0)
      return -ENAMETOOLONG;
  }

  /* Verify directory exists */
  stat_t st;
  ret = ir0_stat_path_routed(new_path, &st);

  if (ret < 0)
    return ret;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  /* In Unix, entering a directory requires execute permission on that path. */
  ret = ir0_access_path_routed(new_path, 1,
                               (uid_t)current_process->euid,
                               (gid_t)current_process->egid);
  if (ret != 0)
    return ret;

  /* Update current working directory */
  strncpy(current_process->cwd, new_path, sizeof(current_process->cwd) - 1);
  current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';

  return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
  size_t len;

  if (!current_process || !buf || size == 0)
    return -EFAULT;

  if (validate_userspace_buffer(buf, size) != 0)
    return -EFAULT;

  len = strlen(current_process->cwd);
  if (len >= size)
    return -ERANGE;

  if (copy_to_user(buf, current_process->cwd, len + 1) != 0)
    return -EFAULT;

  return (int64_t)len;
}

int64_t sys_utimensat(int dirfd, const char *pathname,
                      const struct timespec *times, int flags)
{
  char resolved[256];
  struct timespec ktimes[2];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd);
  if (rc != 0)
    return rc;

  if (times)
  {
    if (validate_userspace_buffer(times, sizeof(ktimes)) != 0)
      return -EFAULT;
    if (copy_from_user(ktimes, times, sizeof(ktimes)) != 0)
      return -EFAULT;
    return ir0_utimensat_path(resolved, ktimes, flags);
  }

  return ir0_utimensat_path(resolved, NULL, flags);
}

int64_t sys_unlink(const char *pathname)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_unlink(resolved);
}

int64_t sys_truncate(const char *pathname, off_t length)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;
  if (length < 0)
    return -EINVAL;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                            current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_truncate(resolved, (size_t)length);
}

/* Linux dirent structure for getdents/getdents64 */
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

/* Directory entry types */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

/**
 * sys_getdents - Get directory entries (POSIX getdents)
 * @fd: File descriptor of open directory
 * @dirent: Buffer to store directory entries
 * @count: Size of buffer in bytes
 *
 * Returns: Number of bytes read, 0 on end of directory, negative on error
 */
int64_t sys_getdents(int fd, void *dirent, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (!dirent || count == 0)
    return -EINVAL;
  
  if (validate_userspace_buffer(dirent, count) != 0)
    return -EFAULT;
  
  /* Handle /proc/pid directory - OSDev-style getdents */
  if (fd == 1150 || fd == 1151)
  {
    int64_t ret = proc_getdents(fd, dirent, count);
    return ret;
  }

  /* Other /proc and /dev - they don't use getdents */
  if (fd >= FD_PROC_BASE && fd < FD_SYS_BASE)
    return -ENOTDIR;  /* These are special file descriptors, not directories */

  fd_entry_t *fd_table = get_process_fd_table();
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;
  
  /* Check if this is a directory */
  const char *path = fd_table[fd].path;
  if (!path)
    return -EBADF;
  
  /* Get stat to verify it's a directory */
  stat_t st;
  if (ir0_stat_path_routed(path, &st) < 0)
    return -ENOTDIR;
  
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;
  
  /* Route readdir through pseudo-fs facade. */
  struct vfs_dirent entries[64];
  int entry_count;
  entry_count = ir0_getdents_path_routed(path, entries, 64);
  
  if (entry_count < 0)
    return entry_count;
  
  /* Convert to linux_dirent64 format */
  char kernel_buf[4096];
  size_t buf_offset = 0;
  
  for (int i = 0; i < entry_count && buf_offset + sizeof(struct linux_dirent64) + 256 < sizeof(kernel_buf); i++)
  {
    if (strcmp(entries[i].name, ".") == 0 || strcmp(entries[i].name, "..") == 0)
      continue;

    size_t name_len = strlen(entries[i].name) + 1;
    /* POSIX: dirent name cannot contain '/' or invalid length (Linux verify_dirent_name) */
    if (name_len <= 1 || name_len >= 256 || strchr(entries[i].name, '/'))
      continue;  /* Skip corrupt entry, show rest */
    size_t reclen = ((sizeof(struct linux_dirent64) + name_len + 7) & ~7);  /* Align to 8 bytes */
    
    if (buf_offset + reclen > sizeof(kernel_buf))
      break;
    
    struct linux_dirent64 *dent = (struct linux_dirent64 *)(kernel_buf + buf_offset);
    dent->d_ino = (uint64_t)(i + 1);
    dent->d_off = (int64_t)(buf_offset + reclen);
    dent->d_reclen = (unsigned short)reclen;
    
    /* Determine type from entry or stat */
    dent->d_type = DT_UNKNOWN;
    if (entries[i].type != 0)
    {
      dent->d_type = entries[i].type;
    }
    else
    {
      /* Try to determine type from stat */
      char full_path[512];
      size_t path_len = strlen(path);
      if (path_len > 0 && path[path_len - 1] == '/')
        snprintf(full_path, sizeof(full_path), "%s%s", path, entries[i].name);
      else
        snprintf(full_path, sizeof(full_path), "%s/%s", path, entries[i].name);
      stat_t entry_st;
      if (ir0_stat_path_routed(full_path, &entry_st) >= 0)
      {
        if (S_ISDIR(entry_st.st_mode))
          dent->d_type = DT_DIR;
        else if (S_ISREG(entry_st.st_mode))
          dent->d_type = DT_REG;
        else if (S_ISCHR(entry_st.st_mode))
          dent->d_type = DT_CHR;
        else if (S_ISBLK(entry_st.st_mode))
          dent->d_type = DT_BLK;
        else if (S_ISLNK(entry_st.st_mode))
          dent->d_type = DT_LNK;
      }
    }
    
    strcpy(dent->d_name, entries[i].name);
    buf_offset += reclen;
  }
  
  /* Use fd offset as read cursor - return 0 when all entries already returned */
  size_t read_offset = (size_t)fd_table[fd].offset;
  if (read_offset >= buf_offset)
    return 0;  /* End of directory */
  
  /* Copy next chunk to user space */
  size_t end_offset = read_offset;
  while (end_offset < buf_offset)
  {
    struct linux_dirent64 *dent = (struct linux_dirent64 *)(kernel_buf + end_offset);
    size_t reclen = (size_t)dent->d_reclen;

    if (reclen < sizeof(struct linux_dirent64) || end_offset + reclen > buf_offset)
      return -EIO;
    if ((end_offset + reclen - read_offset) > count)
      break;
    end_offset += reclen;
  }

  if (end_offset == read_offset)
    return -EINVAL; /* Buffer too small for one complete dirent record */

  size_t copy_size = end_offset - read_offset;
  if (copy_to_user(dirent, kernel_buf + read_offset, copy_size) != 0)
    return -EFAULT;
  
  fd_table[fd].offset = end_offset;
  return (int64_t)copy_size;
}

void syscalls_init(void)
{
  init_syscall_table();
  /* Connect to REAL process management only */
  serial_print("SERIAL: syscalls_init: using REAL process management\n");

  /* Initialize user subsystem */
  /* User system is now handled by permissions system */

  /* Debug: check real process system */
  process_t *real_current = current_process;
  process_t *real_list = get_process_list();

  serial_print("SERIAL: Real current_process = ");
  serial_print_hex32((uint32_t)(uintptr_t)real_current);
  serial_print("\n");

  serial_print("SERIAL: Real process_list = ");
  serial_print_hex32((uint32_t)(uintptr_t)real_list);
  serial_print("\n");
}

/* Stub for unimplemented syscalls (musl ABI compatibility) */
static int64_t sys_nosys(uint64_t a1, uint64_t a2, uint64_t a3,
                         uint64_t a4, uint64_t a5, uint64_t a6)
{
  (void)a1; (void)a2; (void)a3; (void)a4; (void)a5; (void)a6;
  return -ENOSYS;
}

/* Syscall handler type: 6 args for Linux ABI (arg6 for mmap, etc.) */
typedef int64_t (*syscall_handler_t)(uint64_t, uint64_t, uint64_t,
                                     uint64_t, uint64_t, uint64_t);

/* Wrappers to adapt IR0 handlers to uniform 6-arg signature */
#define WRAP1(h, cast1) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return h((cast1)a1); }
#define WRAP2(h, c1, c2) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a3;(void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2); }
#define WRAP3(h, c1, c2, c3) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a4;(void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3); }
#define WRAP4(h, c1, c2, c3, c4) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a5;(void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4); }
#define WRAP5(h, c1, c2, c3, c4, c5) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    (void)a6; return h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5); }
#define WRAP6(h, c1, c2, c3, c4, c5, c6) \
  static int64_t wrap_##h(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) { \
    return (int64_t)h((c1)a1, (c2)a2, (c3)a3, (c4)a4, (c5)a5, (c6)a6); }

WRAP1(sys_exit, int)
WRAP3(sys_read, int, void *, size_t)
WRAP3(sys_write, int, const void *, size_t)
WRAP3(sys_readv, int, const struct iovec *, int)
WRAP3(sys_writev, int, const struct iovec *, int)
WRAP3(sys_open, const char *, int, mode_t)
WRAP1(sys_close, int)
WRAP3(sys_waitpid, pid_t, int *, int)
WRAP2(sys_link, const char *, const char *)
WRAP2(sys_rename, const char *, const char *)
WRAP2(sys_truncate, const char *, off_t)
WRAP1(sys_unlink, const char *)
WRAP3(sys_unlinkat, int, const char *, int)
WRAP4(sys_renameat, int, const char *, int, const char *)
WRAP1(sys_uname, struct utsname *)
WRAP2(sys_access, const char *, int)
WRAP4(sys_faccessat, int, const char *, int, int)
WRAP1(sys_dup, int)
WRAP3(sys_exec, const char *, char *const *, char *const *)
WRAP1(sys_chdir, const char *)
WRAP3(sys_mount, const char *, const char *, const char *)
WRAP2(sys_umount, const char *, int)
WRAP2(sys_mkdir, const char *, mode_t)
WRAP1(sys_rmdir, const char *)
WRAP2(sys_chmod, const char *, mode_t)
WRAP3(sys_chown, const char *, uid_t, gid_t)
WRAP3(sys_lseek, int, off_t, int)
WRAP2(sys_getcwd, char *, size_t)
WRAP4(sys_utimensat, int, const char *, const struct timespec *, int)
WRAP2(sys_stat, const char *, stat_t *)
WRAP2(sys_fstat, int, stat_t *)
WRAP2(sys_dup2, int, int)
WRAP1(sys_brk, void *)
WRAP6(sys_mmap, void *, size_t, int, int, int, off_t)
WRAP2(sys_munmap, void *, size_t)
WRAP3(sys_mprotect, void *, size_t, int)
WRAP2(sys_kill, pid_t, int)
WRAP3(sys_sigaction, int, const struct sigaction *, struct sigaction *)
WRAP4(sys_rt_sigprocmask, int, const uint64_t *, uint64_t *, size_t)
WRAP2(sys_arch_prctl, int, unsigned long)
WRAP1(sys_set_tid_address, int *)
WRAP3(sys_fcntl, int, int, unsigned long)
WRAP4(sys_openat, int, const char *, int, mode_t)
WRAP4(sys_newfstatat, int, const char *, stat_t *, int)
WRAP2(sys_clock_gettime, int, struct timespec *)
WRAP6(sys_futex, int *, int, int, const struct timespec *, int *, int)
WRAP3(sys_getrandom, void *, size_t, unsigned int)
WRAP2(sys_set_robust_list, struct robust_list_head *, size_t)
WRAP4(sys_prlimit64, pid_t, unsigned int, const void *, void *)
WRAP2(sys_pipe2, int *, int)
WRAP1(sys_pipe, int *)
WRAP1(sys_sigreturn, struct sigcontext *)
WRAP3(sys_ioctl, int, uint64_t, void *)
WRAP3(sys_getdents, int, void *, size_t)
WRAP3(sys_poll, struct pollfd *, unsigned int, int)
WRAP2(sys_nanosleep, const struct timespec *, struct timespec *)
WRAP2(sys_gettimeofday, struct timeval *, void *)
WRAP1(sys_setuid, uid_t)
WRAP1(sys_setgid, gid_t)
WRAP1(sys_umask, mode_t)
WRAP1(sys_sudo_auth, const char *)
WRAP5(sys_clone, unsigned long, void *, int *, int *, unsigned long)

#undef WRAP1
#undef WRAP2
#undef WRAP3
#undef WRAP4
#undef WRAP5
#undef WRAP6

/* WRAP0 for no-arg handlers */
static int64_t wrap_sys_fork(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_fork(); }
static int64_t wrap_sys_getpid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getpid(); }
static int64_t wrap_sys_gettid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_gettid(); }
static int64_t wrap_sys_getppid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getppid(); }
static int64_t wrap_sys_getuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getuid(); }
static int64_t wrap_sys_geteuid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_geteuid(); }
static int64_t wrap_sys_getgid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getgid(); }
static int64_t wrap_sys_getegid(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6; return sys_getegid(); }

/* Console scroll: IR0 custom syscall */
static int64_t wrap_console_scroll(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_scroll((int)a1);
  return 0;
}

/* Console clear: IR0 custom syscall */
static int64_t wrap_console_clear(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  console_backend_clear((uint8_t)a1);
  return 0;
}

/* Keyboard layout set/get: IR0 custom syscalls */
static int64_t wrap_keymap_set(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return keyboard_set_layout((int)a1);
}

static int64_t wrap_keymap_get(uint64_t a1, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6) {
  (void)a1;(void)a2;(void)a3;(void)a4;(void)a5;(void)a6;
  return keyboard_get_layout();
}

/* Syscall table: Linux x86-64 numbers -> handlers */
static syscall_handler_t syscall_table_rw[__NR_syscall_max];

static void init_syscall_table(void)
{
  for (size_t i = 0; i < __NR_syscall_max; i++)
    syscall_table_rw[i] = sys_nosys;

  /* Implemented syscalls - Linux numbers */
  syscall_table_rw[__NR_read]           = wrap_sys_read;
  syscall_table_rw[__NR_readv]          = wrap_sys_readv;
  syscall_table_rw[__NR_write]          = wrap_sys_write;
  syscall_table_rw[__NR_writev]         = wrap_sys_writev;
  syscall_table_rw[__NR_open]           = wrap_sys_open;
  syscall_table_rw[__NR_close]          = wrap_sys_close;
  syscall_table_rw[__NR_stat]           = wrap_sys_stat;
  syscall_table_rw[__NR_lstat]          = wrap_sys_stat;
  syscall_table_rw[__NR_fstat]          = wrap_sys_fstat;
  syscall_table_rw[__NR_poll]           = wrap_sys_poll;
  syscall_table_rw[__NR_lseek]          = wrap_sys_lseek;
  syscall_table_rw[__NR_mmap]           = wrap_sys_mmap;
  syscall_table_rw[__NR_mprotect]       = wrap_sys_mprotect;
  syscall_table_rw[__NR_munmap]         = wrap_sys_munmap;
  syscall_table_rw[__NR_brk]            = wrap_sys_brk;
  syscall_table_rw[__NR_rt_sigaction]   = wrap_sys_sigaction;
  syscall_table_rw[__NR_rt_sigprocmask] = wrap_sys_rt_sigprocmask;
  syscall_table_rw[__NR_rt_sigreturn]   = wrap_sys_sigreturn;
  syscall_table_rw[__NR_fcntl]            = wrap_sys_fcntl;
  syscall_table_rw[__NR_ioctl]          = wrap_sys_ioctl;
  syscall_table_rw[__NR_pipe]           = wrap_sys_pipe;
  syscall_table_rw[__NR_pipe2]          = wrap_sys_pipe2;
  syscall_table_rw[__NR_dup2]           = wrap_sys_dup2;
  syscall_table_rw[__NR_nanosleep]      = wrap_sys_nanosleep;
  syscall_table_rw[__NR_getpid]         = wrap_sys_getpid;
  syscall_table_rw[__NR_gettid]         = wrap_sys_gettid;
  syscall_table_rw[__NR_getuid]         = wrap_sys_getuid;
  syscall_table_rw[__NR_geteuid]        = wrap_sys_geteuid;
  syscall_table_rw[__NR_getgid]         = wrap_sys_getgid;
  syscall_table_rw[__NR_getegid]        = wrap_sys_getegid;
  syscall_table_rw[__NR_setuid]         = wrap_sys_setuid;
  syscall_table_rw[__NR_setgid]         = wrap_sys_setgid;
  syscall_table_rw[__NR_umask]          = wrap_sys_umask;
  syscall_table_rw[__NR_sudo_auth]      = wrap_sys_sudo_auth;
  syscall_table_rw[__NR_clone]           = wrap_sys_clone;
  syscall_table_rw[__NR_fork]          = wrap_sys_fork;
  syscall_table_rw[__NR_execve]        = wrap_sys_exec;
  syscall_table_rw[__NR_exit]           = wrap_sys_exit;
  syscall_table_rw[__NR_wait4]          = wrap_sys_waitpid;
  syscall_table_rw[__NR_kill]           = wrap_sys_kill;
  syscall_table_rw[__NR_getdents]       = wrap_sys_getdents;
  syscall_table_rw[__NR_getcwd]         = wrap_sys_getcwd;
  syscall_table_rw[__NR_utimensat]      = wrap_sys_utimensat;
  syscall_table_rw[__NR_chdir]          = wrap_sys_chdir;
  syscall_table_rw[__NR_mkdir]          = wrap_sys_mkdir;
  syscall_table_rw[__NR_rmdir]          = wrap_sys_rmdir;
  syscall_table_rw[__NR_link]           = wrap_sys_link;
  syscall_table_rw[__NR_rename]         = wrap_sys_rename;
  syscall_table_rw[__NR_unlink]         = wrap_sys_unlink;
  syscall_table_rw[__NR_truncate]       = wrap_sys_truncate;
  syscall_table_rw[__NR_unlinkat]       = wrap_sys_unlinkat;
  syscall_table_rw[__NR_renameat]       = wrap_sys_renameat;
  syscall_table_rw[__NR_uname]          = wrap_sys_uname;
  syscall_table_rw[__NR_access]         = wrap_sys_access;
  syscall_table_rw[__NR_faccessat]      = wrap_sys_faccessat;
  syscall_table_rw[__NR_dup]            = wrap_sys_dup;
  syscall_table_rw[__NR_chmod]         = wrap_sys_chmod;
  syscall_table_rw[__NR_chown]          = wrap_sys_chown;
  syscall_table_rw[__NR_gettimeofday]   = wrap_sys_gettimeofday;
  syscall_table_rw[__NR_getppid]        = wrap_sys_getppid;
  syscall_table_rw[__NR_arch_prctl]     = wrap_sys_arch_prctl;
  syscall_table_rw[__NR_set_tid_address] = wrap_sys_set_tid_address;
  syscall_table_rw[__NR_openat]         = wrap_sys_openat;
  syscall_table_rw[__NR_getdents64]     = wrap_sys_getdents;
  syscall_table_rw[__NR_newfstatat]     = wrap_sys_newfstatat;
  syscall_table_rw[__NR_futex]          = wrap_sys_futex;
  syscall_table_rw[__NR_clock_gettime]  = wrap_sys_clock_gettime;
  syscall_table_rw[__NR_set_robust_list] = wrap_sys_set_robust_list;
  syscall_table_rw[__NR_getrandom]      = wrap_sys_getrandom;
  syscall_table_rw[__NR_prlimit64]      = wrap_sys_prlimit64;
  syscall_table_rw[__NR_mount]          = wrap_sys_mount;
  syscall_table_rw[__NR_umount2]        = wrap_sys_umount;
  syscall_table_rw[__NR_exit_group]     = wrap_sys_exit;
  syscall_table_rw[__NR_console_scroll]  = wrap_console_scroll;
  syscall_table_rw[__NR_console_clear]   = wrap_console_clear;
  syscall_table_rw[__NR_keymap_set]      = wrap_keymap_set;
  syscall_table_rw[__NR_keymap_get]      = wrap_keymap_get;

  /*
   * Socket API (__NR_socket .. __NR_getsockopt): no in-kernel socket layer yet.
   * Explicit ENOSYS so musl and libc probes get a deterministic answer.
   */
  {
    static const unsigned socket_nrs[] = {
      __NR_socket, __NR_connect, __NR_accept, __NR_sendto, __NR_recvfrom,
      __NR_sendmsg, __NR_recvmsg, __NR_shutdown, __NR_bind, __NR_listen,
      __NR_getsockname, __NR_getpeername, __NR_socketpair, __NR_setsockopt,
      __NR_getsockopt,
    };
    size_t si;

    for (si = 0; si < sizeof(socket_nrs) / sizeof(socket_nrs[0]); si++)
      syscall_table_rw[socket_nrs[si]] = sys_nosys;
  }
}

/* Syscall dispatcher called from assembly */
#if defined(__x86_64__) || defined(__amd64__)
static void syscall_capture_user_frame(process_t *p)
{
  process_capture_syscall_frame(p);
}
#endif

/**
 * syscall_dispatch - Dispatch system call via table (Linux/musl ABI)
 * @syscall_num: Linux x86-64 syscall number
 * @arg1-arg6: System call arguments (arg6 on stack per AMD64 SysV ABI)
 *
 * Returns: System call return value, or -ENOSYS for unknown/unimplemented
 */
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5,
                         uint64_t arg6)
{
  int64_t r;
  static int fase10_count = 0;
  pid_t cur_pid = current_process ? current_process->task.pid : 0;
  int do_trace = (fase10_count < 5 && cur_pid >= 2);

#if defined(__x86_64__) || defined(__amd64__)
  if (current_process)
    syscall_capture_user_frame(current_process);

  if (current_process && current_process->mode == USER_MODE)
  {
    fork_ret_first_syscall_entry(syscall_num,
                                 current_process->syscall_frame.rip,
                                 current_process->syscall_frame.rsp);
  }
#endif

  if (do_trace) {
    extern uint64_t iretq_checkpoint_buf[40];
    serial_print("FASE10 pre pid=");
    serial_print_hex32((uint32_t)cur_pid);
    serial_print(" nr=");
    serial_print_hex64(syscall_num);
    serial_print(" irq_ck36=");
    serial_print_hex64(iretq_checkpoint_buf[36]);
    serial_print(" irq_pre=");
    serial_print_hex64(iretq_checkpoint_buf[37]);
    serial_print(" irq_ck38=");
    serial_print_hex64(iretq_checkpoint_buf[38]);
    serial_print(" irq_post=");
    serial_print_hex64(iretq_checkpoint_buf[39]);
    serial_print(" iret_id_class=");
    serial_print_hex64(iretq_checkpoint_buf[26]);
    serial_print(" rsp_pre_pop=");
    serial_print_hex64(iretq_checkpoint_buf[28]);
    serial_print(" rsp_pre_iret=");
    serial_print_hex64(iretq_checkpoint_buf[29]);
    serial_print(" q0=");
    serial_print_hex64(iretq_checkpoint_buf[16]);
    serial_print(" q1=");
    serial_print_hex64(iretq_checkpoint_buf[17]);
    serial_print(" q2=");
    serial_print_hex64(iretq_checkpoint_buf[18]);
    serial_print(" q3=");
    serial_print_hex64(iretq_checkpoint_buf[19]);
    serial_print(" q4=");
    serial_print_hex64(iretq_checkpoint_buf[20]);
    serial_print(" q5=");
    serial_print_hex64(iretq_checkpoint_buf[21]);
    serial_print(" q6=");
    serial_print_hex64(iretq_checkpoint_buf[22]);
    serial_print(" q7=");
    serial_print_hex64(iretq_checkpoint_buf[23]);
    serial_print(" q8=");
    serial_print_hex64(iretq_checkpoint_buf[24]);
    serial_print(" q9=");
    serial_print_hex64(iretq_checkpoint_buf[25]);
    serial_print("\n");
  }

  if (syscall_num >= __NR_syscall_max)
    return -ENOSYS;

  ir0_fase58j_note_syscall(syscall_num);

  syscall_handler_t handler = syscall_table_rw[syscall_num];
  r = handler(arg1, arg2, arg3, arg4, arg5, arg6);

  if (do_trace) {
    fase10_count++;
    serial_print("FASE10 post pid=");
    serial_print_hex32((uint32_t)cur_pid);
    serial_print(" nr=");
    serial_print_hex64(syscall_num);
    serial_print(" retval=");
    serial_print_hex64((uint64_t)r);
    serial_print("\n");
  }

  return r;
}
