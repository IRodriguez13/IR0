/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs_rw.c
 * Description: read/write/readv/writev syscalls.
 */

#include "fs_syscalls_internal.h"

static void ash_smoke_read_trace(int fd, int64_t ret)
{
  if (fd == STDIN_FILENO)
    ir0_ash_smoke_read_return(fd, ret);
}

static void ash_smoke_write_trace(int fd, const void *data, size_t count)
{
  if ((fd != STDOUT_FILENO && fd != STDERR_FILENO) || !data || count == 0)
    return;

  ir0_ash_smoke_scan_write((const char *)data, count);
  ir0_ash_smoke_scan_stdout((const char *)data, count);
}

static devfs_node_t *devfs_node_from_fd(int fd)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return NULL;
  if (!fd_table[fd].is_devfs)
    return NULL;
  ensure_devfs_init();
  return fd_entry_devfs_node(&fd_table[fd]);
}

/*
 * devfs_resolve_read_fd - Bound fd_table slot with is_devfs (no virtual FD_DEV).
 * @offset_out: file offset from fd_table when bound.
 */
static devfs_node_t *devfs_resolve_read_fd(int fd, off_t *offset_out)
{
  fd_entry_t *fd_table = get_process_fd_table();

  if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS && fd_table[fd].in_use &&
      fd_table[fd].is_devfs)
  {
    ensure_devfs_init();
    if (offset_out)
      *offset_out = fd_table[fd].offset;
    return fd_entry_devfs_node(&fd_table[fd]);
  }

  return NULL;
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (count == 0)
    return 0;  /* POSIX: write with count 0 returns 0 */
  if (!buf)
    return -EFAULT;

  /* Validate user buffer for regular files */
  char kernel_buf[PAGE_SIZE_4KB];
  const char *str = NULL;
  size_t copy_size = (count < sizeof(kernel_buf)) ? count : sizeof(kernel_buf);

  if (fd >= 0 && fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fd_entry_pseudo(fdt ? &fdt[fd] : NULL))
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;

      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
	return -EFAULT;
      return pseudo_fs_ops_write((const pseudo_fs_ops_t *)bind->ops, bind->ctx,
				kernel_buf, copy_size);
    }
  }

  {
    devfs_node_t *node = devfs_node_from_fd(fd);
    fd_entry_t *fdt = get_process_fd_table();

    if (node)
    {
      if (!node->ops || !node->ops->write)
        return -EBADF;
      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
        return -EFAULT;
      if (fdt && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
          fdt[fd].in_use &&
          devfs_is_ptmx_device(fdt[fd].dev_device_id))
        return devfs_pty_master_write(fdt[fd].vfs_file, kernel_buf,
                                      copy_size);
      ash_smoke_write_trace(fd, kernel_buf, copy_size);
      return node->ops->write(&node->entry, kernel_buf, copy_size, 0);
    }
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
      /* The active graphical VT owns the framebuffer, including stdio's
       * fast path. Diagnostics remain available on the serial console. */
      if (dev_console_is_graphics())
      {
        if (copy_from_user(kernel_buf, buf, copy_size) != 0)
          return -EFAULT;
        console_backend_write_serial(kernel_buf, copy_size);
        return (int64_t)copy_size;
      }
      /* Unredirected stdio still goes to console backend. */
      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
        return -EFAULT;
      ash_smoke_write_trace(fd, kernel_buf, copy_size);
      str = kernel_buf;
      uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
      {
	size_t done = 0;
	const size_t step = 64;

	/*
	 * Binary dumps (`cat /dev/hda`) can pin the CPU in putchar for
	 * minutes. Check SIGINT between chunks so Ctrl+C is not ignored.
	 */
	while (done < copy_size)
	{
	  size_t n = copy_size - done;

	  if (n > step)
	    n = step;
	  if (current_process &&
	      signals_pause_should_interrupt(current_process))
	  {
	    handle_signals();
	    if (done > 0)
	      return (int64_t)done;
	    return -EINTR;
	  }
	  console_backend_write(str + done, n, color);
	  done += n;
	}
      }
      return (int64_t)copy_size;
    }
    return -EBADF;
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_write((struct ir0_eventfd *)fd_table[fd].vfs_file, buf, count,
			     (fd_table[fd].flags & O_NONBLOCK) != 0);

  /* AF_UNIX/TCP stream sockets: write(2) → sock_stream_send */
  if (fd_entry_socket(&fd_table[fd]) &&
      sock_stream_is(fd_table[fd].vfs_file))
  {
    struct sock_stream *ss = (struct sock_stream *)fd_table[fd].vfs_file;
    size_t total = 0;
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;

    while (total < count)
    {
      size_t chunk = count - total;
      ssize_t n;

      if (chunk > sizeof(kernel_buf))
	chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
	return total > 0 ? (int64_t)total : -EFAULT;
      n = sock_stream_send(ss, kernel_buf, chunk);
      if (n < 0)
	return total > 0 ? (int64_t)total : n;
      if (n == 0)
      {
	if (nb)
	  return total > 0 ? (int64_t)total : -EAGAIN;
	if (signals_pause_should_interrupt(current_process))
	  return total > 0 ? (int64_t)total : -EINTR;
	{
	  int64_t sleep_ret = syscall_sleep_ms_locked(20);

	  if (sleep_ret < 0)
	    return total > 0 ? (int64_t)total : sleep_ret;
	}
	continue;
      }
      total += (size_t)n;
    }
    return (int64_t)total;
  }

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;

    if (!pipe)
      return -EBADF;

    if (fd_table[fd].pipe_end != 1 &&
	(fd_table[fd].flags & O_ACCMODE) != O_RDWR)
      return -EBADF;

    /*
     * Linux pipe(7) / write(2): blocking write completes the full count
     * (atomic ≤ PIPE_BUF via pipe_write, else partial chunks + wait).
     * O_NONBLOCK may short-write after any progress.
     */
    while (total < count)
    {
      size_t chunk = count - total;
      size_t off = 0;

      if (chunk > sizeof(kernel_buf))
	chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
	return total > 0 ? (int64_t)total : -EFAULT;

      while (off < chunk)
      {
	ret = pipe_write(pipe, kernel_buf + off, chunk - off);
	if (ret > 0)
	{
	  off += (size_t)ret;
	  total += (size_t)ret;
	  pipe_wake_all(pipe);
	  continue;
	}
	if (ret == -EPIPE)
	{
	  /*
	   * Queue SIGPIPE unless SIG_IGN (Linux signal(7)). User handlers
	   * still get the signal; write returns -EPIPE if no bytes yet,
	   * else short success (Linux pipe_write keeps prior progress).
	   */
	  if (!(current_process->signal_ignored & SIGNAL_MASK(SIGPIPE)))
	    current_process->signal_pending |= SIGNAL_MASK(SIGPIPE);
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EPIPE;
	}
	if (ret != -EAGAIN)
	  return total > 0 ? (int64_t)total : ret;
	if (nb)
	  return total > 0 ? (int64_t)total : -EAGAIN;
	if (pipe->readers <= 0)
	{
	  if (!(current_process->signal_ignored & SIGNAL_MASK(SIGPIPE)))
	    current_process->signal_pending |= SIGNAL_MASK(SIGPIPE);
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EPIPE;
	}
	if (signals_pause_should_interrupt(current_process))
	{
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EINTR;
	}
	{
	  int wait_ret = pipe_wait(current_process, pipe, 0,
				     chunk - off);

	  if (wait_ret == -EINTR)
	  {
	    handle_signals();
	    return total > 0 ? (int64_t)total : -EINTR;
	  }
	  if (wait_ret != 0)
	    return total > 0 ? (int64_t)total : -EAGAIN;
	}
	if (signals_pause_should_interrupt(current_process))
	{
	  handle_signals();
	  return total > 0 ? (int64_t)total : -EINTR;
	}
      }
    }
    process_clear_in_thread_syscall_block(current_process);
    return (int64_t)total;
  }

  /* Check write permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_WRITE, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;

    while (total < count)
    {
      size_t chunk = count - total;

      if (chunk > sizeof(kernel_buf))
        chunk = sizeof(kernel_buf);
      if (copy_from_user(kernel_buf, (const char *)buf + total, chunk) != 0)
        return total > 0 ? (int64_t)total : -EFAULT;
      ret = vfs_write(vfs_file, kernel_buf, chunk);
      if (ret < 0)
        return total > 0 ? (int64_t)total : ret;
      if (ret == 0)
        break;
      total += (size_t)ret;
      if ((size_t)ret < chunk)
        break;
    }
    fd_table[fd].offset = vfs_file->pos;
    return (int64_t)total;
  }

  return -EBADF;
}

/*
 * sys_writev - POSIX writev(2) via repeated sys_write (musl stdio flush path).
 */
int64_t sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
  int64_t total = 0;
  int i;

  if (!current_process)
    return -ESRCH;
  if (!iov)
    return -EFAULT;
  if (iovcnt <= 0)
    return -EINVAL;
  if (iovcnt > IR0_UIO_MAXIOV)
    return -EINVAL;
  if (!access_ok(iov, (size_t)iovcnt * sizeof(struct iovec)))
    return -EFAULT;

  for (i = 0; i < iovcnt; i++)
  {
    struct iovec kiov;
    int64_t ret;

    if (copy_from_user(&kiov, &iov[i], sizeof(kiov)) != 0)
      return total > 0 ? total : -EFAULT;
    if (kiov.iov_len == 0)
      continue;
    ret = sys_write(fd, kiov.iov_base, kiov.iov_len);
    if (ret < 0)
      return total > 0 ? total : ret;
    total += ret;
    if ((size_t)ret < kiov.iov_len)
      break;
  }

  return total;
}

int64_t sys_read(int fd, void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (count == 0)
    return 0;  /* POSIX: read with count 0 returns 0 */
  if (!buf)
    return -EFAULT;

  /* Process-local pseudo_fs binds (/proc|/sys|/heart) — real fd_table slots. */
  if (fd >= 0 && fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fdt && fdt[fd].in_use && (fdt[fd].flags & O_DIRECTORY) &&
	!fdt[fd].is_pipe && !fdt[fd].is_socket)
    {
      /* Directory fds (incl. O_DIRECTORY opens) are not readable via read(2). */
      return -EISDIR;
    }

    if (fdt && fdt[fd].in_use && fdt[fd].is_pseudo && fdt[fd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;
      char kernel_read_buf[PAGE_SIZE_4KB];
      size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
      off_t off = (off_t)fdt[fd].offset;
      int64_t ret;

      ret = pseudo_fs_ops_read((const pseudo_fs_ops_t *)bind->ops, bind->ctx,
			       kernel_read_buf, read_size, &off);
      if (ret > 0)
      {
	if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
	  return -EFAULT;
	fdt[fd].offset = (uint64_t)off;
      }
      return ret;
    }
  }

  /* /dev: bound fd_table slot (is_devfs) */
  {
    off_t read_off = 0;
    devfs_node_t *node = devfs_resolve_read_fd(fd, &read_off);
    fd_entry_t *fd_table = get_process_fd_table();

    if (node)
    {
      char kernel_read_buf[PAGE_SIZE_4KB];
      size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
      int ret;

      if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
	  fd_table[fd].in_use &&
	  devfs_is_ptmx_device(fd_table[fd].dev_device_id))
      {
	int nb = (fd_table[fd].flags & IR0_O_NONBLOCK) ? 1 : 0;

	for (;;)
	{
	  ret = (int)devfs_pty_master_read(fd_table[fd].vfs_file,
					   kernel_read_buf, read_size);
	  if (ret != 0 || nb ||
	      devfs_pty_master_can_read(fd_table[fd].vfs_file))
	    break;
	  if (signals_pause_should_interrupt(current_process))
	    return -EINTR;
	  {
	    int64_t sleep_ret = syscall_sleep_ms_locked(20);

	    if (sleep_ret < 0)
	      return sleep_ret;
	  }
	}
      }
      else if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
	  fd_table[fd].in_use && fd_table[fd].vfs_file &&
	  devfs_node_wants_text_snap(fd_table[fd].dev_device_id))
      {
	ret = (int)devfs_text_snap_read(
		(const devfs_text_snap_t *)fd_table[fd].vfs_file,
		kernel_read_buf, read_size, read_off);
      }
      else
      {
	if (!node->ops || !node->ops->read)
	  return -EBADF;
	{
	  int nb = (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
		    fd_table[fd].in_use &&
		    (fd_table[fd].flags & IR0_O_NONBLOCK)) ? 1 : 0;

	  for (;;)
	  {
	    devfs_set_read_nonblock(nb);
	    ret = node->ops->read(&node->entry, kernel_read_buf, read_size, read_off);
	    devfs_set_read_nonblock(0);
	    if (ret != 0 || nb || !node->ops->can_read ||
	        node->ops->can_read(&node->entry, current_process->task.pid))
	      break;
	    if (signals_pause_should_interrupt(current_process))
	      return -EINTR;
	    {
	      int64_t sleep_ret = syscall_sleep_ms_locked(20);

	      if (sleep_ret < 0)
	        return sleep_ret;
	    }
	  }
	}
      }
      if (ret > 0)
      {
        if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
          return -EFAULT;
        if (fd_table && fd >= 0 && fd < MAX_FDS_PER_PROCESS && fd_table[fd].in_use)
          fd_table[fd].offset += ret;
        if (fd == STDIN_FILENO)
        {
          d1_12_read_diag_kcopy(ret, count, kernel_read_buf, (size_t)ret);
          ash_smoke_read_trace(fd, ret);
        }
      }
      else if (fd == STDIN_FILENO)
      {
        ash_smoke_read_trace(fd, ret);
      }
      return ret;
    }
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd != STDIN_FILENO)
      return -EBADF;
    {
      int64_t ret = syscalls_read_stdio_stdin(buf, count);

      ash_smoke_read_trace(STDIN_FILENO, ret);
      return ret;
    }
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_eventfd && fd_table[fd].vfs_file)
    return ir0_eventfd_read((struct ir0_eventfd *)fd_table[fd].vfs_file, buf, count,
			    (fd_table[fd].flags & O_NONBLOCK) != 0);
  if (fd_table[fd].is_timerfd && fd_table[fd].vfs_file)
    return ir0_timerfd_read((struct ir0_timerfd *)fd_table[fd].vfs_file, buf, count,
			    (fd_table[fd].flags & O_NONBLOCK) != 0);

  /* AF_UNIX/TCP stream sockets: read(2) → sock_stream_recv (blocking). */
  if (fd_table[fd].is_socket && fd_table[fd].vfs_file &&
      sock_stream_is(fd_table[fd].vfs_file))
  {
    struct sock_stream *ss = (struct sock_stream *)fd_table[fd].vfs_file;
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    int nb = (fd_table[fd].flags & O_NONBLOCK) != 0;
    ssize_t n;

    /*
     * Validate the destination before dequeuing: a failed copy_to_user()
     * must not drop socket bytes (Linux read(2) EFAULT contract).
     */
    if (!access_ok_mapped(buf, read_size))
      return -EFAULT;

    for (;;)
    {
      n = sock_stream_recv_flags(ss, kernel_read_buf, read_size, 0);
      if (n != -EAGAIN)
	break;
      if (nb)
	return -EAGAIN;
      if (signals_pause_should_interrupt(current_process))
	return -EINTR;
      {
	int64_t sleep_ret = syscall_sleep_ms_locked(20);

	if (sleep_ret < 0)
	  return sleep_ret;
      }
    }
    if (n < 0)
      return n;
    if (n > 0 && copy_to_user(buf, kernel_read_buf, (size_t)n) != 0)
      return -EFAULT;
    return n;
  }

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    int ret;

    if (!pipe)
      return -EBADF;

    if (fd_table[fd].pipe_end != 0)
      return -EBADF;

    for (;;)
    {
      ret = pipe_read(pipe, kernel_read_buf, read_size);
      if (ret >= 0)
      {
        pipe_wake_all(pipe);
        if (ret == 0)
        {
          return 0;
        }
        if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        {
          /*
           * pipe_read already consumed the bytes. Put them back so a
           * bad user pointer does not drop the pipe contents. Linux
           * copies into the user buffer before advancing the ring.
           */
          pipe_unread(pipe, kernel_read_buf, (size_t)ret);
          return -EFAULT;
        }
        process_clear_in_thread_syscall_block(current_process);
        return ret;
      }
      if (ret != -EAGAIN)
        return ret;
      if (fd_table[fd].flags & O_NONBLOCK)
        return -EAGAIN;
      /*
       * No "writers are gone, report EOF" shortcut here. This runs after
       * pipe_read() already released the pipe, so the task can be preempted
       * in between: the writer then fills the buffer and exits, and the
       * shortcut sees writers==0 and returns EOF while the bytes it just
       * wrote are still queued. Looping back into pipe_read() decides both
       * cases together, as Linux pipe_read does under the pipe mutex —
       * buffered data first, EOF only on an empty pipe with no writers.
       */
      if (signals_pause_should_interrupt(current_process))
      {
	handle_signals();
	return -EINTR;
      }
      {
	int wait_ret = pipe_wait(current_process, pipe, 1, 0);

	if (wait_ret == -EINTR)
	{
	  handle_signals();
	  return -EINTR;
	}
	if (wait_ret != 0)
	  return -EAGAIN;
      }
      if (signals_pause_should_interrupt(current_process))
      {
	handle_signals();
	return -EINTR;
      }
    }
  }

  /* Check read permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_READ, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    char kernel_read_buf[PAGE_SIZE_4KB];
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    size_t total = 0;
    int ret;

    while (total < count)
    {
      size_t chunk = count - total;
      off_t pos_before;

      if (chunk > sizeof(kernel_read_buf))
        chunk = sizeof(kernel_read_buf);
      /*
       * Validate the destination before consuming file data. A failed
       * copy_to_user() must not advance f_pos (Linux read(2) contract).
       */
      if (!access_ok_mapped((char *)buf + total, chunk))
        return total > 0 ? (int64_t)total : -EFAULT;
      pos_before = vfs_file->pos;
      ret = vfs_read(vfs_file, kernel_read_buf, chunk);
      if (ret < 0)
        return total > 0 ? (int64_t)total : ret;
      if (ret == 0)
        break;
      if (copy_to_user((char *)buf + total, kernel_read_buf, (size_t)ret) != 0)
      {
        fd_table[fd].offset = (uint64_t)pos_before;
        vfs_file->pos = pos_before;
        return total > 0 ? (int64_t)total : -EFAULT;
      }
      total += (size_t)ret;
      if ((size_t)ret < chunk)
        break;
    }
    fd_table[fd].offset = vfs_file->pos;
    return (int64_t)total;
  }

  return -EBADF;
}

/*
 * sys_readv - POSIX readv(2) via repeated sys_read (mirror of sys_writev).
 */
int64_t sys_readv(int fd, const struct iovec *iov, int iovcnt)
{
  int64_t total = 0;
  int i;

  if (!current_process)
    return -ESRCH;
  if (!iov)
    return -EFAULT;
  if (iovcnt <= 0)
    return -EINVAL;
  if (iovcnt > IR0_UIO_MAXIOV)
    return -EINVAL;
  if (!access_ok(iov, (size_t)iovcnt * sizeof(struct iovec)))
    return -EFAULT;

  for (i = 0; i < iovcnt; i++)
  {
    struct iovec kiov;
    int64_t ret;

    /* Linux stream readv(2) blocks only until some data is available.  Once
     * progress exists, do not block merely because an earlier iovec happened
     * to fill exactly; X11 setup replies commonly arrive fragmented here. */
    if (total > 0)
    {
      fd_entry_t *table = get_process_fd_table();

      if (table && fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
          table[fd].in_use && table[fd].is_socket &&
          !fd_can_read_for(current_process, fd))
        break;
    }

    if (copy_from_user(&kiov, &iov[i], sizeof(kiov)) != 0)
      return total > 0 ? total : -EFAULT;
    if (kiov.iov_len == 0)
      continue;
    ret = sys_read(fd, kiov.iov_base, kiov.iov_len);
    if (ret < 0)
      return total > 0 ? total : ret;
    total += ret;
    if ((size_t)ret < kiov.iov_len)
      break;
  }

  return total;
}
