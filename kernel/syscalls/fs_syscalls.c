// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — file I/O syscalls (split from syscalls.c).
 */

#include "../syscalls.h"
#include "../process.h"
#include "fs_syscalls.h"
#include "syscalls_glue.h"
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/console_backend.h>
#include <ir0/devfs.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/open_flags.h>
#include <ir0/path.h>
#include <ir0/path_routed.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/pipe.h>
#include <ir0/procfs.h>
#include <ir0/serial_io.h>
#include <ir0/sysfs.h>
#include <ir0/uio.h>
#include <ir0/validation.h>
#include <fs/vfs.h>
#include <kernel/elf_loader.h>
#include <kernel/process.h>
#include <ir0/fase50_debug.h>
#include <ir0/fase51_debug.h>
#include <ir0/fase52_debug.h>
#include <ir0/utimens.h>
#include <mm/paging.h>
#include <string.h>

#define AT_REMOVEDIR 0x200

static void fase50c_log_open_result(const char *path, int64_t ret, int stage)
{
#if CONFIG_DEBUG_FASE50
  const char *tag;

  serial_print("[FASE50C][OPEN] stage=");
  serial_print_hex64((uint64_t)stage);
  serial_print(" path=");
  serial_print(path ? path : "(null)");
  serial_print(" ret=");
  serial_print_hex64((uint64_t)ret);
  serial_print("\n");

  if (!path || strncmp(path, "/f50_", 5) != 0 || ret >= 0)
    return;

  if (ret == -EINVAL)
    tag = "FILE_CREATE_FLAKE_FLAGS";
  else if (ret == -EACCES || ret == -EPERM)
    tag = "FILE_CREATE_FLAKE_PERMISSION";
  else if (ret == -EEXIST)
    tag = "FILE_CREATE_FLAKE_EXISTING_FILE";
  else if (ret == -EMFILE)
    tag = "FILE_CREATE_FLAKE_HARNESS_SETUP";
  else
    tag = "FILE_CREATE_FLAKE_HARNESS_SETUP";

  serial_print("[FASE50B][OPEN_CLASSIFY] ");
  serial_print(tag);
  serial_print("\n");
#else
  (void)path;
  (void)ret;
  (void)stage;
#endif
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
  
  /* Handle /proc file descriptors (FD_PROC_BASE .. FD_DEV_BASE) */
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
  {
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;
    return proc_write(fd, kernel_buf, copy_size);
  }

  /* Handle /dev file descriptors (FD_DEV_BASE .. FD_SYS_BASE) */
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    ensure_devfs_init();
    uint32_t device_id = (uint32_t)(fd - FD_DEV_BASE);
    devfs_node_t *node = devfs_find_node_by_id(device_id);
    if (!node || !node->ops || !node->ops->write)
      return -EBADF;
    
    /* Copy from user space for device writes */
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;
    return node->ops->write(&node->entry, kernel_buf, copy_size, 0);
  }

  /* Handle /sys file descriptors (FD_SYS_BASE .. FD_SYS_BASE + FD_RANGE_SIZE) */
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
  {
    /* Copy from user space for sysfs writes */
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;
    
    return sysfs_write(fd, kernel_buf, copy_size);
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
      /* Unredirected stdio still goes to console backend. */
      if (copy_from_user(kernel_buf, buf, copy_size) != 0)
        return -EFAULT;
#if CONFIG_DEBUG_FASE50
      if (current_process->task.pid >= 2)
      {
        serial_print("[FASE50B][WRITE] pid=");
        serial_print_hex32((uint32_t)current_process->task.pid);
        serial_print(" fd=");
        serial_print_hex64((uint64_t)fd);
        serial_print(" redirected=0\n");
        serial_print("[FASE50B][CLASSIFY] STDOUT_FD_NOT_PIPE\n");
      }
#endif
      str = kernel_buf;
      uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
      console_backend_write(str, copy_size, color);
      if (current_process->task.pid == 1 && copy_size >= 10 &&
	  kernel_buf[0] == 'F' && !memcmp(kernel_buf, "FASE48_IPC", 10))
	process_fase48_ipc_summary("fase48-final");
      if (current_process->task.pid == 1 && copy_size >= 11 &&
	  kernel_buf[0] == 'F' && !memcmp(kernel_buf, "FASE49_PIPE", 11))
	pipe_fase49_classify();
      return (int64_t)copy_size;
    }
    return -EBADF;
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    int ret;

    if (!pipe)
      return -EBADF;

    if (fd_table[fd].pipe_end != 1)
      return -EBADF;

    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;

#if CONFIG_DEBUG_FASE50
    if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
    {
      serial_print("[FASE50B][WRITE] pid=");
      serial_print_hex32((uint32_t)current_process->task.pid);
      serial_print(" fd=");
      serial_print_hex64((uint64_t)fd);
      serial_print(" user_buf=");
      serial_print_hex64((uint64_t)(uintptr_t)buf);
      serial_print(" count=");
      serial_print_hex64((uint64_t)copy_size);
      serial_print(" copied=");
      serial_print_hex64((uint64_t)copy_size);
      fase50b_dump_bytes("[FASE50B][WRITE] payload", kernel_buf, copy_size);
      serial_print("[FASE50B][WRITE] pipe_id=");
      serial_print_hex64(pipe->pipe_id);
      serial_print(" end=");
      serial_print_hex64((uint64_t)fd_table[fd].pipe_end);
      serial_print(" fd_refs=");
      serial_print_hex64((uint64_t)pipe->fd_refs);
      serial_print(" redirected=1\n");
    }
#endif

    for (;;)
    {
      ret = pipe_write(pipe, kernel_buf, copy_size);
      if (ret >= 0)
      {
#if CONFIG_DEBUG_FASE50
        if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
        {
          serial_print("[FASE50B][WRITE] pipe_write_ret=");
          serial_print_hex64((uint64_t)ret);
          serial_print("\n");
        }
#endif
        pipe_wake_all(pipe);
        fase51_dbg_pipe_rw("write", fd, ret);
        fase52_dbg_rw("write", fd, ret);
        return ret;
      }
      if (ret == -EPIPE)
        return -EPIPE;
      if (ret != -EAGAIN)
        return ret;
      if (fd_table[fd].flags & O_NONBLOCK)
        return -EAGAIN;
      if (pipe->readers <= 0)
        return -EPIPE;
      if (pipe_wait(current_process, pipe, 0) != 0)
        return -EAGAIN;
    }
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
    fase52_dbg_rw("write", fd, (int)total);
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
  if (validate_userspace_buffer(iov,
                                (size_t)iovcnt * sizeof(struct iovec)) != 0)
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

  /* Handle /proc file descriptors (FD_PROC_BASE .. FD_DEV_BASE) */
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE) {
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    off_t offset = proc_get_offset(fd);
    int ret = proc_read(fd, kernel_read_buf, read_size, offset);
    if (ret > 0) {
      /* Copy to user space */
      if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        return -EFAULT;
      proc_set_offset(fd, offset + ret);
    }
    return ret;
  }

  /* Handle /sys file descriptors (FD_SYS_BASE .. FD_SYS_BASE + FD_RANGE_SIZE) */
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE) {
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    off_t offset = proc_get_offset(fd);  /* Reuse proc offset tracking */
    int ret = sysfs_read(fd, kernel_read_buf, read_size, offset);
    if (ret > 0) {
      /* Copy to user space */
      if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        return -EFAULT;
      proc_set_offset(fd, offset + ret);  /* Reuse proc offset tracking */
    }
    return ret;
  }

  /* Handle /dev file descriptors (FD_DEV_BASE .. FD_SYS_BASE) */
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
  {
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    ensure_devfs_init();
    uint32_t device_id = (uint32_t)(fd - FD_DEV_BASE);
    devfs_node_t *node = devfs_find_node_by_id(device_id);
    if (!node || !node->ops || !node->ops->read)
      return -EBADF;
    int ret = node->ops->read(&node->entry, kernel_read_buf, read_size, 0);
    if (ret > 0) {
      /* Copy to user space */
      if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        return -EFAULT;
    }
    return ret;
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd >= STDIN_FILENO && fd <= STDERR_FILENO && !stdio_is_redirected(fd_table, fd))
  {
    if (fd != STDIN_FILENO)
      return -EBADF;
    return syscalls_read_stdio_stdin(buf, count);
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

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
#if CONFIG_DEBUG_FASE50
          serial_print("[FASE50B][READ] pid=");
          serial_print_hex32((uint32_t)current_process->task.pid);
          serial_print(" fd=");
          serial_print_hex64((uint64_t)fd);
          serial_print(" rsi=");
          serial_print_hex64((uint64_t)(uintptr_t)buf);
          serial_print(" count=");
          serial_print_hex64((uint64_t)count);
          serial_print(" ret=0 eof\n");
#endif
          fase51_dbg_pipe_rw("read", fd, 0);
          return 0;
        }
        if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
          return -EFAULT;
#if CONFIG_DEBUG_FASE50
        serial_print("[FASE50B][READ] pid=");
        serial_print_hex32((uint32_t)current_process->task.pid);
        serial_print(" fd=");
        serial_print_hex64((uint64_t)fd);
        serial_print(" rsi=");
        serial_print_hex64((uint64_t)(uintptr_t)buf);
        serial_print(" count=");
        serial_print_hex64((uint64_t)count);
        serial_print(" ret=");
        serial_print_hex64((uint64_t)ret);
        serial_print(" pipe_id=");
        serial_print_hex64(pipe->pipe_id);
        serial_print("\n");
        fase50b_dump_bytes("[FASE50B][READ] copied", kernel_read_buf, (size_t)ret);
#endif
        fase51_dbg_pipe_rw("read", fd, ret);
        fase52_dbg_rw("read", fd, ret);
        return ret;
      }
      if (ret != -EAGAIN)
        return ret;
      if (fd_table[fd].flags & O_NONBLOCK)
        return -EAGAIN;
      if (pipe->writers <= 0)
        return 0;
      if (pipe_wait(current_process, pipe, 1) != 0)
        return -EAGAIN;
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

      if (chunk > sizeof(kernel_read_buf))
        chunk = sizeof(kernel_read_buf);
      ret = vfs_read(vfs_file, kernel_read_buf, chunk);
      if (ret < 0)
        return total > 0 ? (int64_t)total : ret;
      if (ret == 0)
        break;
      if (copy_to_user((char *)buf + total, kernel_read_buf, (size_t)ret) != 0)
        return total > 0 ? (int64_t)total : -EFAULT;
      total += (size_t)ret;
      if ((size_t)ret < chunk)
        break;
    }
    fd_table[fd].offset = vfs_file->pos;
    fase52_dbg_rw("read", fd, (int)total);
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
  if (validate_userspace_buffer(iov,
                                (size_t)iovcnt * sizeof(struct iovec)) != 0)
    return -EFAULT;

  for (i = 0; i < iovcnt; i++)
  {
    struct iovec kiov;
    int64_t ret;

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

int64_t sys_fstat(int fd, stat_t *buf)
{
  if (!current_process || !buf)
    return -EFAULT;

  /* Validate buffer is in userspace (for USER_MODE processes) */
  if (validate_userspace_buffer(buf, sizeof(stat_t)) != 0)
    return -EFAULT;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  /* Handle standard file descriptors */
  if (fd <= 2)
  {
    /* Standard streams - fill with basic info */
    buf->st_dev = 0;
    buf->st_ino = fd;
    buf->st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_size = 0;
    buf->st_atime = 0;
    buf->st_mtime = 0;
    buf->st_ctime = 0;
    return 0;
  }

  /* For regular files, get info from filesystem via VFS */
  return vfs_stat(fd_table[fd].path, buf);
}

/* Open file and return file descriptor */
int64_t sys_open(const char *pathname, int flags, mode_t mode)
{
  char path_copy[256];
  int ir0_flags;
  int linux_open_flags = flags;
  size_t plen;
  int64_t open_ret;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if (copy_from_user_cstring(path_copy, sizeof(path_copy), pathname) != 0)
    return -EFAULT;
  path_copy[sizeof(path_copy) - 1] = '\0';

  plen = 0;
  while (path_copy[plen] && plen < sizeof(path_copy))
    plen++;
  if (plen == 0 || plen >= sizeof(path_copy))
    return -EINVAL;

  ir0_flags = linux_open_flags_to_ir0(flags);
  ir0_open_flags_log_translation(flags, ir0_flags);
  if (!ir0_open_flags_ok_for_vfs(ir0_flags))
  {
    fase50c_log_open_result(path_copy, -EINVAL, 0);
    return -EINVAL;
  }

#if CONFIG_DEBUG_FASE50
  serial_print("[FASE50C][OPEN] pid=");
  serial_print_hex32((uint32_t)current_process->task.pid);
  serial_print(" path=");
  serial_print(path_copy);
  serial_print(" mode=");
  serial_print_hex64((uint64_t)mode);
  serial_print("\n");
#endif

  /* Handle /proc filesystem on-demand */
  if (is_proc_path(path_copy))
  {
    open_ret = proc_open(path_copy, ir0_flags);
    fase50c_log_open_result(path_copy, open_ret, 1);
    return open_ret;
  }

  /* Handle /sys filesystem on-demand */
  if (is_sys_path(path_copy))
  {
    open_ret = sysfs_open(path_copy, ir0_flags);
    fase50c_log_open_result(path_copy, open_ret, 2);
    return open_ret;
  }

  /* Handle /dev filesystem on-demand */
  if (path_copy[0] == '/' &&
      path_copy[1] == 'd' &&
      path_copy[2] == 'e' &&
      path_copy[3] == 'v' &&
      path_copy[4] == '/')
  {
    int64_t drc;

    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node(path_copy);
    if (!node)
    {
      fase50c_log_open_result(path_copy, -ENOENT, 3);
      return -ENOENT;
    }
    drc = devfs_open_node(node, ir0_flags);
    if (drc < 0)
    {
      fase50c_log_open_result(path_copy, drc, 4);
      return drc;
    }
    open_ret = FD_DEV_BASE + (int64_t)node->entry.device_id;
    fase50c_log_open_result(path_copy, open_ret, 5);
    return open_ret;
  }

  /* Resolve against cwd via path facade */
  char resolved_path[256];
  const char *path_to_use = path_copy;
  int path_rc;

  path_rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, path_copy, resolved_path,
                                 sizeof(resolved_path), current_process->cwd);
  if (path_rc != 0)
    return path_rc;
  path_to_use = resolved_path;

  flags = ir0_flags;

  /* Check access permissions based on flags */
  if (flags & O_DIRECTORY)
  {
    if (!check_file_access(path_to_use, ACCESS_EXEC, current_process))
    {
      fase50c_log_open_result(path_to_use, -EACCES, 12);
      return -EACCES;
    }
  }
  else if (flags & O_CREAT)
  {
    stat_t st;

    /*
     * Non-existent target: check parent directory (search + write).
     * Existing target: same access rules as a normal open.
     */
    if (vfs_stat(path_to_use, &st) != 0)
    {
      char parent[256];

      if (get_parent_path(path_to_use, parent, sizeof(parent)) != 0)
        return -ENAMETOOLONG;
      if (!check_file_access(parent, ACCESS_EXEC | ACCESS_WRITE, current_process))
      {
        fase50c_log_open_result(path_to_use, -EACCES, 9);
        return -EACCES;
      }
    }
    else
    {
      int access_mode = 0;
      int accmode = flags & O_ACCMODE;

      if (accmode == O_RDONLY || accmode == O_RDWR)
        access_mode |= ACCESS_READ;
      if (accmode == O_WRONLY || accmode == O_RDWR)
        access_mode |= ACCESS_WRITE;
      if (access_mode &&
          !check_file_access(path_to_use, access_mode, current_process))
      {
        fase50c_log_open_result(path_to_use, -EACCES, 10);
        return -EACCES;
      }
    }
  }
  else
  {
    int access_mode = 0;
    int accmode = flags & O_ACCMODE;
    if (accmode == O_RDONLY || accmode == O_RDWR)
      access_mode |= ACCESS_READ;
    if (accmode == O_WRONLY || accmode == O_RDWR)
      access_mode |= ACCESS_WRITE;
    if (access_mode && !check_file_access(path_to_use, access_mode, current_process))
    {
      fase50c_log_open_result(path_to_use, -EACCES, 11);
      return -EACCES;
    }
  }

  fd_entry_t *fd_table = get_process_fd_table();

  int fd = -1;
  for (int i = 3; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }

  if (fd == -1)
  {
    fase50c_log_open_result(path_to_use, -EMFILE, 6);
    return -EMFILE;
  }

  struct vfs_file *vfs_file = NULL;
  int ret = vfs_open(path_to_use, flags, mode, &vfs_file);
  if (ret != 0)
  {
    fase50c_log_open_result(path_to_use, (int64_t)ret, 7);
    return ret;  /* Return actual error, not just -ENOENT */
  }

  if (flags & O_DIRECTORY)
  {
    stat_t st;
    if (sys_stat(path_to_use, &st) < 0)
    {
      if (vfs_file)
      {
        vfs_close(vfs_file);
      }
      return -ENOTDIR;
    }
    if (!S_ISDIR(st.st_mode))
    {
      if (vfs_file)
      {
        vfs_close(vfs_file);
      }
      return -ENOTDIR;
    }
  }

  /* Set up file descriptor with real VFS file handle */
  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path_to_use, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = flags;
  fd_table[fd].vfs_file = vfs_file;
  fd_table[fd].offset = vfs_file ? vfs_file->pos : 0;
  fase48_note_fd_created();

  fase50c_log_open_result(path_to_use, (int64_t)fd, 8);
#if CONFIG_DEBUG_FASE50
  if (kstrcmp(path_to_use, "/f50_file.txt") == 0 && fd >= 0)
  {
    stat_t pst;

    if (vfs_stat(path_to_use, &pst) == 0 && S_ISREG(pst.st_mode))
      serial_print("[FASE50C][CLASSIFY] FILE_CREATE_STILL_OK\n");
    else
      serial_print("[FASE50C][CLASSIFY] CREATED_AS_WRONG_TYPE\n");
  }
#endif
  if (path_to_use && strncmp(path_to_use, "/f51_", 5) == 0)
    fase51_dbg_open_redirect(path_to_use, linux_open_flags, (int64_t)fd);
  fase52_dbg_openat(IR0_AT_FDCWD, path_to_use, linux_open_flags, (int64_t)fd);
  return fd;
}

int64_t sys_stat(const char *pathname, stat_t *buf)
{
  char resolved[256];
  stat_t kst;
  int64_t rc;

  if (!current_process || !pathname || !buf)
    return -EFAULT;

  if (validate_userspace_buffer(buf, sizeof(stat_t)) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  rc = ir0_stat_path_routed(resolved, &kst);

  if (rc != 0)
    return rc;
  if (copy_to_user(buf, &kst, sizeof(kst)) != 0)
    return -EFAULT;
  fase52_dbg_stat_path(resolved, 0);
  return 0;
}

/*
 * sys_openat - Linux openat(2) subset (AT_FDCWD only).
 */
int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
  if (!current_process)
    return -ESRCH;
  if (dirfd != IR0_AT_FDCWD)
    return -EBADF;
  return sys_open(pathname, flags, mode);
}

/*
 * sys_newfstatat - Linux newfstatat(2) subset.
 */
int64_t sys_newfstatat(int dirfd, const char *pathname, stat_t *buf, int flags)
{
  char resolved[256];
  stat_t kst;
  int64_t rc;

  (void)flags;

  if (!current_process || !buf)
    return -EFAULT;

  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;

  if (validate_userspace_buffer(buf, sizeof(stat_t)) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path_at(dirfd, pathname, resolved, sizeof(resolved),
                                current_process->cwd);
  if (rc != 0)
    return rc;

  rc = ir0_stat_path_routed(resolved, &kst);

  if (rc != 0)
    return rc;
  if (copy_to_user(buf, &kst, sizeof(kst)) != 0)
    return -EFAULT;
  return 0;
}

int64_t sys_unlinkat(int dirfd, const char *pathname, int flags)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;
  if (flags & ~AT_REMOVEDIR)
    return -EINVAL;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_user_path_at(dirfd, pathname, resolved, sizeof(resolved),
                                current_process->cwd);
  if (rc != 0)
    return rc;

  if (flags & AT_REMOVEDIR)
    return vfs_rmdir(resolved);
  return vfs_unlink(resolved);
}

int64_t sys_renameat(int olddirfd, const char *oldpath,
                     int newdirfd, const char *newpath)
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

  rc = ir0_resolve_user_path_at(olddirfd, oldpath, old_resolved,
                                sizeof(old_resolved), current_process->cwd);
  if (rc != 0)
    return rc;
  rc = ir0_resolve_user_path_at(newdirfd, newpath, new_resolved,
                                sizeof(new_resolved), current_process->cwd);
  if (rc != 0)
    return rc;

  return vfs_rename(old_resolved, new_resolved);
}
