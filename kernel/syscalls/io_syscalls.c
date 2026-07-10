/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: io_syscalls.c
 * Description: I/O wait syscalls (poll/select/pause/nanosleep)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "io_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/poll.h>
#include <ir0/select.h>
#include <ir0/time.h>
#include <ir0/console.h>
#include <ir0/devfs.h>
#include <ir0/scheduler_api.h>
#include <ir0/ktm.h>
#include <ir0/fcntl.h>
#include <ir0/clock.h>
#include <ir0/clock_wait.h>
#include <ir0/signals.h>
#include <ir0/arch_port.h>
#include <ir0/paging.h>
#include <config.h>

#include <ir0/pipe.h>
#include <ir0/vfs.h>
#include <ir0/devfs.h>
#include <ir0/pseudo_fs.h>
#include <ir0/sock_udp.h>
#include <ir0/fase51_debug.h>
#include <ir0/fase52_debug.h>
#include <ir0/serial_io.h>
#include <ir0/copy_user.h>
#include <ir0/paging.h>
#include <string.h>

static int devfs_initialized;

static int64_t sys_pipe_install(int pipefd[2], int flags);


extern void kernel_idle_poll(void);
extern void kernel_idle_poll_nosched(void);

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

static int poll_wake_do(void);

/*
 * syscall_sleep_ms_locked - Block current task for @ms (kernel-side, no user copy).
 */
static int64_t syscall_sleep_ms_locked(uint64_t ms)
{
	uint64_t now;
	int ret;

	if (!current_process)
		return -ESRCH;
	if (ms == 0)
		return 0;

	now = clock_get_uptime_milliseconds();
	ret = ir0_clock_wait_block_until(now + ms);
	if (ret < 0)
		return ret;
	return 0;
}

/**
 * fd_can_read_for - Non-blocking read readiness for @proc's fd table.
 */
static int fd_can_read_for(process_t *proc, int fd)
{
  fd_entry_t *fd_table = proc ? proc->fd_table : get_process_fd_table();
  pid_t pid = proc ? proc->task.pid : 0;

  if (fd == STDIN_FILENO && !stdio_is_redirected(fd_table, fd))
    return ir0_console_input_ready() ? 1 : 0;
  if ((fd == STDOUT_FILENO || fd == STDERR_FILENO) && !stdio_is_redirected(fd_table, fd))
    return 0;
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
    return 1;
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
    return devfs_fd_can_read((uint32_t)(fd - FD_DEV_BASE), pid);
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return 1;
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_devfs)
    return devfs_fd_can_read(fd_table[fd].dev_device_id, pid) ? 1 : 0;
  if (fd_table[fd].is_pipe && fd_table[fd].pipe_end == 0) {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    return (pipe && (pipe->count > 0 || pipe->writers <= 0)) ? 1 : 0;
  }
  if (fd_table[fd].flags & (O_RDONLY | O_RDWR))
    return 1;
  return 0;
}

/**
 * fd_can_read - Comprueba si el fd tiene datos para leer (sin bloquear).
 */
static int fd_can_read(int fd)
{
  return fd_can_read_for(current_process, fd);
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

static int fd_can_write_for(process_t *proc, int fd)
{
	process_t *saved = current_process;
	int ret;

	if (!proc)
		return fd_can_write(fd);
	current_process = proc;
	ret = fd_can_write(fd);
	current_process = saved;
	return ret;
}

/**
 * poll_check_ready_for - poll(2) readiness scan for @proc's fd table.
 */
static int poll_check_ready_for(process_t *proc, struct pollfd *fds, unsigned int nfds)
{
  int count = 0;
  unsigned int i;

  for (i = 0; i < nfds; i++) {
    fds[i].revents = 0;
    if (fds[i].fd < 0)
      continue;
    if ((fds[i].events & POLLIN) && fd_can_read_for(proc, fds[i].fd))
      fds[i].revents |= POLLIN;
    if ((fds[i].events & POLLOUT) && fd_can_write_for(proc, fds[i].fd))
      fds[i].revents |= POLLOUT;
    if (fds[i].revents)
      count++;
  }
  return count;
}

/**
 * poll_check_ready - Rellena revents y devuelve cuántos fd tienen eventos.
 */
static int poll_check_ready(struct pollfd *fds, unsigned int nfds)
{
  return poll_check_ready_for(current_process, fds, nfds);
}

int64_t sys_poll(struct pollfd *user_fds, unsigned int nfds, int timeout_ms)
{
  if (!current_process)
    return -ESRCH;
  if (nfds > MAX_POLL_NFDS)
    return -EINVAL;

  /*
   * Linux: poll(NULL, 0, timeout) is a millisecond sleep (runsvdir iopause).
   */
  if (nfds == 0)
  {
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  if (!user_fds)
    return -EFAULT;
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
  for (i = 0; i < MAX_POLL_WAITERS; i++)
  {
    if (!poll_waiters[i].proc)
    {
      w = &poll_waiters[i];
      break;
    }
  }
  if (!w)
  {
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
  process_arm_kernel_syscall_sleep(current_process);
  current_process->state = PROCESS_BLOCKED;

  while (current_process->state == PROCESS_BLOCKED)
  {
    ir0_clock_wait_service_runqueue();
    if (current_process->state != PROCESS_BLOCKED)
      break;
  }

  process_restore_user_task_segments(current_process);

  if (current_process->syscall_interrupted)
  {
    current_process->syscall_interrupted = 0;
    if (w)
    {
      kfree(w->kfds);
      w->proc = NULL;
      w->kfds = NULL;
    }
    current_process->poll_waiter = NULL;
    return -EINTR;
  }

  w = (struct poll_waiter *)current_process->poll_waiter;
  current_process->poll_waiter = NULL;
  if (w)
  {
    ready = w->ready_count;
    if (ready >= 0 && w->kfds &&
        copy_to_user(user_fds, w->kfds, nfds * sizeof(struct pollfd)) != 0)
      ready = -EFAULT;
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
 * sys_select - POSIX select(2) via poll readiness checks (tier-1 / libc compat).
 * ARCH_DEBT: move to kernel/syscalls/io_syscalls.c when syscalls.c is split.
 */
int64_t sys_select(int nfds, fd_set *user_r, fd_set *user_w, fd_set *user_e,
                   struct timeval *user_tv)
{
  fd_set kr;
  fd_set kw;
  fd_set ke;
  struct pollfd pfds[MAX_POLL_NFDS];
  unsigned int npoll = 0;
  int timeout_ms = -1;
  struct timeval tv;
  uint64_t expire;
  int ready;
  unsigned int i;
  int fd;

  if (!current_process)
    return -ESRCH;
  if (nfds < 0 || nfds > MAX_POLL_NFDS)
    return -EINVAL;
  if (!user_r && !user_w && !user_e && !user_tv)
    return -EINVAL;

  IR0_FD_ZERO(&kr);
  IR0_FD_ZERO(&kw);
  IR0_FD_ZERO(&ke);

  if (user_r)
  {
    if (validate_userspace_buffer(user_r, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&kr, user_r, sizeof(fd_set)) != 0)
      return -EFAULT;
  }
  if (user_w)
  {
    if (validate_userspace_buffer(user_w, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&kw, user_w, sizeof(fd_set)) != 0)
      return -EFAULT;
  }
  if (user_e)
  {
    if (validate_userspace_buffer(user_e, sizeof(fd_set)) != 0)
      return -EFAULT;
    if (copy_from_user(&ke, user_e, sizeof(fd_set)) != 0)
      return -EFAULT;
  }
  if (user_tv)
  {
    if (validate_userspace_buffer(user_tv, sizeof(tv)) != 0)
      return -EFAULT;
    if (copy_from_user(&tv, user_tv, sizeof(tv)) != 0)
      return -EFAULT;
    if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000)
      return -EINVAL;
    timeout_ms = (int)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
  }

  if (nfds == 0)
  {
    if (!user_tv)
      return -EINVAL;
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  for (fd = 0; fd < nfds; fd++)
  {
    short ev = 0;

    if (user_r && IR0_FD_ISSET(fd, &kr))
      ev |= POLLIN;
    if (user_w && IR0_FD_ISSET(fd, &kw))
      ev |= POLLOUT;
    if (user_e && IR0_FD_ISSET(fd, &ke))
      ev |= POLLERR | POLLHUP;
    if (!ev)
      continue;
    if (npoll >= MAX_POLL_NFDS)
      return -EINVAL;
    pfds[npoll].fd = fd;
    pfds[npoll].events = ev;
    pfds[npoll].revents = 0;
    npoll++;
  }

  if (npoll == 0)
  {
    if (timeout_ms == 0)
      return 0;
    if (timeout_ms < 0)
    {
      for (;;)
      {
        int64_t ret = syscall_sleep_ms_locked(1000);

        if (ret < 0)
          return ret;
        if (current_process->signal_pending != 0)
          return 0;
      }
    }
    return syscall_sleep_ms_locked((uint64_t)timeout_ms);
  }

  expire = (timeout_ms < 0) ? (uint64_t)-1
                            : (clock_get_uptime_milliseconds() + (uint64_t)timeout_ms);
  ready = 0;
  for (;;)
  {
    ready = poll_check_ready(pfds, npoll);
    if (ready > 0 || timeout_ms == 0)
      break;
    if (timeout_ms >= 0 && expire != (uint64_t)-1 &&
        clock_get_uptime_milliseconds() >= expire)
      break;
    if (current_process->signal_pending != 0)
      break;

    {
      int64_t ret = syscall_sleep_ms_locked(50);

      if (ret < 0)
        return ret;
    }
  }

  IR0_FD_ZERO(&kr);
  IR0_FD_ZERO(&kw);
  IR0_FD_ZERO(&ke);
  for (i = 0; i < npoll; i++)
  {
    fd = pfds[i].fd;
    if (user_r && (pfds[i].revents & (POLLIN | POLLHUP | POLLERR)))
      IR0_FD_SET(fd, &kr);
    if (user_w && (pfds[i].revents & POLLOUT))
      IR0_FD_SET(fd, &kw);
    if (user_e && (pfds[i].revents & (POLLERR | POLLHUP)))
      IR0_FD_SET(fd, &ke);
  }

  if (user_r && copy_to_user(user_r, &kr, sizeof(fd_set)) != 0)
    return -EFAULT;
  if (user_w && copy_to_user(user_w, &kw, sizeof(fd_set)) != 0)
    return -EFAULT;
  if (user_e && copy_to_user(user_e, &ke, sizeof(fd_set)) != 0)
    return -EFAULT;

  return (int64_t)ready;
}

static int poll_wake_do(void)
{
  uint64_t now = clock_get_uptime_milliseconds();
  unsigned int i;
  int woke = 0;

  for (i = 0; i < MAX_POLL_WAITERS; i++) {
    struct poll_waiter *w = &poll_waiters[i];

    if (!w->proc)
      continue;
    int ready = poll_check_ready_for(w->proc, w->kfds, w->nfds);
    int timeout = (w->timeout_expire != (uint64_t)-1 && now >= w->timeout_expire);
    if (ready > 0 || timeout) {
      w->ready_count = ready;
      w->woken = 1;
      if (w->proc->state == PROCESS_BLOCKED)
      {
        w->proc->state = PROCESS_READY;
        sched_add_process(w->proc);
        sched_promote_process(w->proc);
      }
      woke = 1;
    }
  }
  return woke;
}

int poll_wake_check_nosched(void)
{
	return poll_wake_do();
}

void poll_wake_check(void)
{
  if (poll_wake_do())
    sched_schedule_next();
}

void syscall_wake_blocked_on_child(process_t *parent)
{
	int was_blocked;
	int wait_armed;

	if (!parent)
		return;

	was_blocked = (parent->state == PROCESS_BLOCKED);
	wait_armed = (parent->irq_frame_saved && !parent->poll_waiter);
	if (!was_blocked && !wait_armed && !parent->poll_waiter)
		return;

	ir0_clock_wait_disarm(parent);

	if (parent->poll_waiter)
		(void)poll_wake_do();

	if (was_blocked || wait_armed)
	{
		parent->state = PROCESS_READY;
		sched_add_process(parent);
		sched_promote_process(parent);
	}
}

/*
 * syscall_poll_finish_blocked_resume - Copy poll revents and set syscall retval.
 * Called from arch_context_switch irq resume with parent CR3 loaded.
 */
void syscall_poll_finish_blocked_resume(process_t *proc)
{
	struct poll_waiter *w;
	int ready;

	if (!proc)
		return;

	w = (struct poll_waiter *)proc->poll_waiter;
	if (!w || !w->kfds)
		return;

	ready = w->ready_count;
	if (ready < 0)
		ready = poll_check_ready_for(proc, w->kfds, w->nfds);
	if (ready < 0)
		ready = 0;

	if (w->user_fds && proc->page_directory)
	{
		if (copy_to_user_region_in_directory(proc->page_directory,
						     (uintptr_t)w->user_fds,
						     w->kfds,
						     w->nfds * sizeof(struct pollfd)) != 0)
			ready = -EFAULT;
	}

	kfree(w->kfds);
	w->proc = NULL;
	w->kfds = NULL;
	w->user_fds = NULL;
	w->nfds = 0;
	w->woken = 0;
	w->ready_count = 0;
	proc->poll_waiter = NULL;
	proc->poll_resume_via_arch = 0;
}

/**
 * sys_pause - Wait until a signal arrives (POSIX pause(2)).
 */
int64_t sys_pause(void)
{
  if (!current_process)
    return -ESRCH;

  for (;;)
  {
    if (signals_pause_should_interrupt(current_process))
    {
      handle_signals();
      return -EINTR;
    }

    current_process->state = PROCESS_BLOCKED;
    process_arm_kernel_syscall_sleep(current_process);
    while (current_process->state == PROCESS_BLOCKED)
    {
      ir0_clock_wait_service_runqueue();
      if (current_process->state != PROCESS_BLOCKED)
        break;
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
  int64_t ret = syscall_sleep_ms_locked(ms);

  if (ret < 0)
    return ret;
  if (rem)
  {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

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
#if CONFIG_DEBUG_FASE50
	fase48_fd_created++;
#endif
}

void fase48_note_fd_destroyed(void)
{
#if CONFIG_DEBUG_FASE50
	fase48_fd_destroyed++;
#endif
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
    else if (fd_table[fd].is_socket && fd_table[fd].vfs_file)
    {
      sock_udp_release((struct sock_udp *)fd_table[fd].vfs_file);
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
    fd_table[fd].is_socket = false;
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
  /*
   * Linux dup(2)/dup2(2): the close-on-exec flag for the duplicate is always
   * cleared (man 2 dup). dup3() is the path that can set O_CLOEXEC atomically.
   */
  fd_table[newfd].fd_flags = 0;
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
  {
    int start = (int)arg;
    int i;

    if (start < 0 || start >= MAX_FDS_PER_PROCESS)
      return -EINVAL;
    for (i = start; i < MAX_FDS_PER_PROCESS; i++)
    {
      if (!fd_table[i].in_use)
        return sys_dup2(fd, i);
    }
    return -EMFILE;
  }
  default:
    return -EINVAL;
  }
}
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
