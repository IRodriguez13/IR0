/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: IR0 kernel source/header file
 */

// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: syscalls.c
 * Description: System call POSIX ABI interface source module
 * 
 */

#include "syscalls.h"
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
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <ir0/validation.h>
#include <ir0/vga.h>
#include <ir0/stat.h>
#include <kernel/scheduler_api.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ir0/oops.h>
#include <ir0/keyboard.h>
#include <fs/vfs.h>
#include <ir0/path.h>
#include <ir0/driver.h>
#include <ir0/errno.h>
#include <ir0/copy_user.h>
#include <ir0/permissions.h>
#include <ir0/chmod.h>
#include <ir0/fcntl.h>
#include <ir0/procfs.h>
#include <ir0/devfs.h>
#include <ir0/sysfs.h>
#include <ir0/signals.h>
#include <ir0/pipe.h>
#include <ir0/poll.h>
#include <ir0/time.h>
#include <interrupt/arch/idt.h>
#include <ir0/video_backend.h>
#include <ir0/rtc.h>

/* Pseudo file descriptor ranges (/proc, /dev, /sys) */
#define FD_PROC_BASE  1000
#define FD_DEV_BASE   2000
#define FD_SYS_BASE   3000
#define FD_RANGE_SIZE 1000

/* Forward declarations */
static fd_entry_t *get_process_fd_table(void);
int64_t sys_unlink(const char *pathname);
static void init_syscall_table(void);

static int devfs_initialized = 0;

/*
 * read(0) bloqueante: procesos esperando teclado.
 * Se despiertan desde stdin_wake_check en el main loop.
 */
#define MAX_STDIN_WAITERS 8
static process_t *stdin_waiters[MAX_STDIN_WAITERS];

static void ensure_devfs_init(void)
{
  if (!devfs_initialized)
  {
    devfs_init();
    devfs_initialized = 1;
  }
}

static int is_dev_path(const char *path)
{
  return path && strncmp(path, "/dev/", 5) == 0;
}

/**
 * validate_userspace_string - Validate that a string argument is in userspace
 * @str: String pointer to validate
 * @max_len: Maximum expected string length
 * Returns: 0 if valid, -EFAULT if invalid
 */
static int validate_userspace_string(const char *str, size_t max_len)
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
static int validate_userspace_buffer(const void *buf, size_t size)
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

static int stdio_is_redirected(fd_entry_t *fd_table, int fd)
{
  if (!fd_table || fd < STDIN_FILENO || fd > STDERR_FILENO)
    return 0;
  if (!fd_table[fd].in_use)
    return 0;
  if (fd_table[fd].is_pipe)
    return 1;
  if (fd_table[fd].vfs_file)
    return 1;
  return 0;
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
      str = kernel_buf;
      uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
      console_backend_write(str, copy_size, color);
      return (int64_t)copy_size;
    }
    return -EBADF;
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    /* Write to pipe */
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    if (!pipe)
      return -EBADF;

    /* Validate it's the write end */
    if (fd_table[fd].pipe_end != 1)
      return -EBADF; /* Can't write to read end */

    /* Copy from user space */
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;

    /* Write to pipe */
    int ret = pipe_write(pipe, kernel_buf, copy_size);
    return ret;
  }

  /* Check write permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_WRITE, current_process))
    return -EACCES;

  /* Copy from user space for regular file writes */
  if (copy_from_user(kernel_buf, buf, copy_size) != 0)
    return -EFAULT;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    int ret = vfs_write(vfs_file, kernel_buf, copy_size);
    if (ret >= 0)
    {
      fd_table[fd].offset = vfs_file->pos;
      return ret;
    }
    return ret;
  }

  return -EBADF;
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

    /* Unredirected stdin reads from keyboard buffer (non-blocking). */
    char kernel_read_buf[256];
    size_t bytes_read = 0;
    size_t max_read = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);

    if (!keyboard_buffer_has_data())
    {
      int slot = -1;
      for (int i = 0; i < MAX_STDIN_WAITERS; i++)
      {
        if (stdin_waiters[i] == NULL)
        {
          slot = i;
          break;
        }
      }
      if (slot >= 0)
      {
        stdin_waiters[slot] = current_process;
        current_process->state = PROCESS_BLOCKED;
        sched_schedule_next();
      }
      if (!keyboard_buffer_has_data())
        return 0;
    }

    while (bytes_read < max_read && keyboard_buffer_has_data())
    {
      char c = keyboard_buffer_get();
      if (c != 0)
        kernel_read_buf[bytes_read++] = c;
    }

    if (bytes_read > 0)
    {
      if (copy_to_user(buf, kernel_read_buf, bytes_read) != 0)
        return -EFAULT;
      return (int64_t)bytes_read;
    }
    return 0;
  }
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe)
  {
    /* Read from pipe */
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    if (!pipe)
      return -EBADF;

    /* Validate it's the read end */
    if (fd_table[fd].pipe_end != 0)
      return -EBADF; /* Can't read from write end */

    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    
    /* Read from pipe */
    int ret = pipe_read(pipe, kernel_read_buf, read_size);
    if (ret >= 0)
    {
      /* Copy to user space */
      if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        return -EFAULT;
      return ret;
    }
    return ret;
  }

  /* Check read permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_READ, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    int ret = vfs_read(vfs_file, kernel_read_buf, read_size);
    if (ret >= 0)
    {
      /* Copy to user space */
      if (copy_to_user(buf, kernel_read_buf, (size_t)ret) != 0)
        return -EFAULT;
      fd_table[fd].offset = vfs_file->pos;
      return ret;
    }
    return ret;
  }

  return -EBADF;
}

int64_t sys_getpid(void)
{
  if (!current_process)
    return -ESRCH;
  return process_pid(current_process);
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
  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Resolve relative paths against cwd (same as open/stat) */
  char resolved[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }

  return vfs_mkdir(path_to_use, (int)mode);
}

int64_t sys_exec(const char *pathname,
                 char *const argv[],
                 char *const envp[])
{
  serial_print("SERIAL: sys_exec called\n");

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

  /* Validate argv and envp if provided */
  if (argv && !is_user_address(argv, sizeof(char *) * 256))
  {
    return -EFAULT;
  }
  
  if (envp && !is_user_address(envp, sizeof(char *) * 256))
  {
    return -EFAULT;
  }

  /* Copy argv and envp from userspace if provided */
  char *kernel_argv[256] = {NULL};
  char *kernel_envp[256] = {NULL};
  
  if (argv)
  {
    /* Copy argv array */
    char *user_argv[256];
    if (copy_from_user(user_argv, argv, sizeof(char *) * 256) != 0)
      return -EFAULT;
    
    /* Copy each argv string */
    for (int i = 0; i < 256 && user_argv[i]; i++)
    {
      char *arg_str = (char *)kmalloc_try(256);
      if (!arg_str)
      {
        for (int j = 0; j < 256 && kernel_argv[j]; j++)
          kfree(kernel_argv[j]);
        return -ENOMEM;
      }
      if (copy_from_user(arg_str, user_argv[i], 256) == 0)
        kernel_argv[i] = arg_str;
      else
      {
        kfree(arg_str);
        break;
      }
    }
  }
  
  if (envp)
  {
    /* Copy envp array */
    char *user_envp[256];
    if (copy_from_user(user_envp, envp, sizeof(char *) * 256) != 0)
    {
      /* Clean up argv on error */
      for (int i = 0; i < 256 && kernel_argv[i]; i++)
        kfree(kernel_argv[i]);
      return -EFAULT;
    }
    
    /* Copy each envp string */
    for (int i = 0; i < 256 && user_envp[i]; i++)
    {
      char *env_str = (char *)kmalloc_try(256);
      if (!env_str)
      {
        for (int j = 0; j < 256 && kernel_argv[j]; j++)
          kfree(kernel_argv[j]);
        for (int j = 0; j < 256 && kernel_envp[j]; j++)
          kfree(kernel_envp[j]);
        return -ENOMEM;
      }
      if (copy_from_user(env_str, user_envp[i], 256) == 0)
        kernel_envp[i] = env_str;
      else
      {
        kfree(env_str);
        break;
      }
    }
  }

  int64_t result = kexecve(path_to_use,
                           argv ? (char *const *)kernel_argv : NULL,
                           envp ? (char *const *)kernel_envp : NULL);

  /* Clean up copied strings */
  for (int i = 0; i < 256 && kernel_argv[i]; i++)
    kfree(kernel_argv[i]);
  for (int i = 0; i < 256 && kernel_envp[i]; i++)
    kfree(kernel_envp[i]);

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
  if (!current_process || !oldpath || !newpath)
    return -EFAULT;

  /* Validate arguments are in userspace (for USER_MODE processes) */
  if (validate_userspace_string(oldpath, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(newpath, 256) != 0)
    return -EFAULT;

  /* Call link through VFS layer */
  return vfs_link(oldpath, newpath);
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
  if (!current_process || !oldpath || !newpath)
    return -EFAULT;

  if (validate_userspace_string(oldpath, 256) != 0)
    return -EFAULT;
  if (validate_userspace_string(newpath, 256) != 0)
    return -EFAULT;

  char old_resolved[256], new_resolved[256];
  const char *old_use = oldpath;
  const char *new_use = newpath;

  if (!is_absolute_path(oldpath))
  {
    if (join_paths(current_process->cwd, oldpath, old_resolved, sizeof(old_resolved)) != 0)
      return -ENAMETOOLONG;
    old_use = old_resolved;
  }
  else
  {
    if (normalize_path(oldpath, old_resolved, sizeof(old_resolved)) != 0)
      return -ENAMETOOLONG;
    old_use = old_resolved;
  }

  if (!is_absolute_path(newpath))
  {
    if (join_paths(current_process->cwd, newpath, new_resolved, sizeof(new_resolved)) != 0)
      return -ENAMETOOLONG;
    new_use = new_resolved;
  }
  else
  {
    if (normalize_path(newpath, new_resolved, sizeof(new_resolved)) != 0)
      return -ENAMETOOLONG;
    new_use = new_resolved;
  }

  return vfs_rename(old_use, new_use);
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

  stat_t st;
  int stat_ok = 0;
  if (is_proc_path(path_to_use))
    stat_ok = (proc_stat(path_to_use, &st) == 0);
  else if (is_sys_path(path_to_use))
    stat_ok = (sysfs_stat(path_to_use, &st) == 0);
  else if (is_dev_path(path_to_use))
  {
    ensure_devfs_init();
    stat_ok = (devfs_find_node(path_to_use) != NULL);
  }
  else
    stat_ok = (vfs_stat(path_to_use, &st) == 0);

  if (!stat_ok)
    return -ENOENT;

  if (mode == 0) /* F_OK */
    return 0;

  int access_mode = 0;
  if (mode & 4) access_mode |= ACCESS_READ;   /* R_OK */
  if (mode & 2) access_mode |= ACCESS_WRITE; /* W_OK */
  if (mode & 1) access_mode |= ACCESS_EXEC;  /* X_OK */

  if (access_mode && !check_file_access(path_to_use, access_mode, current_process))
    return -EACCES;
  return 0;
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
  if (!current_process || !pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Resolve relative paths against cwd */
  char resolved[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }

  /* POSIX rmdir: only empty dirs. Recursive delete done in userspace (rm -d). */
  return vfs_rmdir(path_to_use);
}

static fd_entry_t *get_process_fd_table(void)
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
    return keyboard_buffer_has_data() ? 1 : 0;
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
    return 1;
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
  sched_schedule_next();

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
    }
  }
  current_process = saved_current;
}

/**
 * stdin_wake_check - Wake processes blocked on read(0) when keyboard has data.
 * Called from main loop.
 */
void stdin_wake_check(void)
{
  if (!keyboard_buffer_has_data())
    return;
  for (int i = 0; i < MAX_STDIN_WAITERS; i++)
  {
    if (stdin_waiters[i])
    {
      stdin_waiters[i]->state = PROCESS_READY;
      stdin_waiters[i] = NULL;
    }
  }
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
  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Handle /proc filesystem on-demand */
  if (is_proc_path(pathname)) {
    return proc_open(pathname, flags);
  }

  /* Handle /sys filesystem on-demand */
  if (is_sys_path(pathname)) {
    return sysfs_open(pathname, flags);
  }

  /* Handle /dev filesystem on-demand */
  if (is_dev_path(pathname))
  {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node(pathname);
    if (!node)
      return -ENOENT;
    return FD_DEV_BASE + (int64_t)node->entry.device_id;
  }

  /* Linux-style: resolve relative paths against cwd before VFS */
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

  /* Check access permissions based on flags */
  if (flags & O_DIRECTORY)
  {
    if (!check_file_access(path_to_use, ACCESS_EXEC, current_process))
      return -EACCES;
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
      return -EACCES;
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
    return -EMFILE;

  struct vfs_file *vfs_file = NULL;
  int ret = vfs_open(path_to_use, flags, mode, &vfs_file);
  if (ret != 0)
  {
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

  return fd;
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

  return -ENOTTY;
}

int64_t sys_close(int fd)
{
  if (!current_process)
    return -ESRCH;

  /* Handle special fd ranges before fd_table bounds check */
  if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
    return 0;  /* /dev devices: no per-fd state to release */
  if (fd >= FD_PROC_BASE && fd < FD_DEV_BASE)
    return 0;  /* /proc */
  if (fd >= FD_SYS_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return 0;  /* /sys */

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd <= 2)
    return -EBADF;

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe && fd_table[fd].vfs_file)
  {
    /* Close pipe end - this decrements ref_count */
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    pipe_close_end(pipe, fd_table[fd].pipe_end);
    fd_table[fd].vfs_file = NULL;
  }
  else if (fd_table[fd].vfs_file)
  {
    /* Close VFS file if it exists */
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    vfs_close(vfs_file);
    fd_table[fd].vfs_file = NULL;
  }

  fd_table[fd].in_use = false;
  fd_table[fd].is_pipe = false;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].path[0] = '\0';
  fd_table[fd].flags = 0;
  fd_table[fd].offset = 0;

  return 0;
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
    return new_offset;
  }

  if (fd_table[fd].vfs_file) {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    off_t result = vfs_lseek(vfs_file, offset, whence);
    if (result < 0)
      return result;
    fd_table[fd].offset = (uint64_t)result;
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
      fd_table[newfd].vfs_file = NULL;
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
  }

  fd_table[newfd].in_use = true;
  strncpy(fd_table[newfd].path, fd_table[oldfd].path, sizeof(fd_table[newfd].path) - 1);
  fd_table[newfd].path[sizeof(fd_table[newfd].path) - 1] = '\0';
  fd_table[newfd].flags = fd_table[oldfd].flags;
  fd_table[newfd].fd_flags = fd_table[oldfd].fd_flags;
  fd_table[newfd].offset = fd_table[oldfd].offset;
  fd_table[newfd].is_pipe = fd_table[oldfd].is_pipe;
  fd_table[newfd].pipe_end = fd_table[oldfd].pipe_end;

  /*
   * Regular files now share one open file description (vfs_file with refcount),
   * matching Unix dup/dup2 offset-sharing semantics.
   * Pipes share one pipe_t and keep per-end counts.
   */
  if (fd_table[oldfd].is_pipe)
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

  return newfd;
}

/* Helper function to get file stats by path (for ls improvement) */
int64_t sys_stat(const char *pathname, stat_t *buf)
{
  if (!current_process || !pathname || !buf)
    return -EFAULT;

  /* Validate arguments are in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;
  if (validate_userspace_buffer(buf, sizeof(stat_t)) != 0)
    return -EFAULT;

  /* Handle /proc filesystem */
  if (is_proc_path(pathname)) {
    return proc_stat(pathname, buf);
  }

  /* Handle /sys filesystem */
  if (is_sys_path(pathname)) {
    return sysfs_stat(pathname, buf);
  }

  /* Handle /dev filesystem */
  if (is_dev_path(pathname)) {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node(pathname);
    if (!node)
      return -ENOENT;

    memset(buf, 0, sizeof(stat_t));
    buf->st_mode = S_IFCHR | (node->entry.mode & 0777);
    buf->st_rdev = node->entry.device_id;
    buf->st_nlink = 1;
    buf->st_uid = 0;
    buf->st_gid = 0;
    buf->st_size = 0;
    buf->st_blksize = 512;
    buf->st_blocks = 0;
    return 0;
  }

  /* Linux-style: resolve relative paths against cwd for VFS */
  char resolved[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  return vfs_stat(path_to_use, buf);
}

/*
 * sys_fork — wired for Linux __NR_fork; implementation is kernel fork().
 *
 * debug_bins/dbgshell.c issues SYS_FORK for pipe setups. The kernel fork does
 * not duplicate the parent memory map or resume the child at the syscall with
 * rax == 0; see fork() in process.c. Pipelines therefore cannot rely on full
 * POSIX fork+exec semantics until real address-space copy exists.
 */
int64_t sys_fork(void)
{
  if (!current_process)
    return -ESRCH;

  return fork();
}


int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  return process_wait(pid, status, options);
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
  if (!current_process)
    return -ESRCH;

  if (!pipefd)
    return -EFAULT;

  /* Validate pipefd is in userspace (for USER_MODE processes) */
  if (validate_userspace_buffer(pipefd, sizeof(int) * 2) != 0)
    return -EFAULT;

  /* Create pipe using existing pipe implementation */
  pipe_t *pipe = pipe_create();
  if (!pipe)
    return -ENOMEM; /* Out of memory */

  /* Find two free file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  int read_fd = -1, write_fd = -1;

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
    /* pipe_create() starts with two refs (read/write ends). */
    pipe_close_end(pipe, 0);
    pipe_close_end(pipe, 1);
    return -EMFILE; /* Too many open files */
  }

  /* Initialize read fd */
  fd_table[read_fd].in_use = 1;
  /* fd is the array index, no need to store it */
  strncpy(fd_table[read_fd].path, "/dev/pipe", sizeof(fd_table[read_fd].path) - 1);
  fd_table[read_fd].path[sizeof(fd_table[read_fd].path) - 1] = '\0';
  fd_table[read_fd].offset = 0;
  fd_table[read_fd].flags = O_RDONLY;
  fd_table[read_fd].vfs_file = (void *)pipe;  /* Store pipe pointer */
  fd_table[read_fd].is_pipe = 1;  /* Mark as pipe */
  fd_table[read_fd].pipe_end = 0;  /* 0 = read end */

  /* Initialize write fd */
  fd_table[write_fd].in_use = 1;
  /* fd is the array index, no need to store it */
  strncpy(fd_table[write_fd].path, "/dev/pipe", sizeof(fd_table[write_fd].path) - 1);
  fd_table[write_fd].path[sizeof(fd_table[write_fd].path) - 1] = '\0';
  fd_table[write_fd].offset = 0;
  fd_table[write_fd].flags = O_WRONLY;
  fd_table[write_fd].vfs_file = (void *)pipe;  /* Store pipe pointer (shared) */
  fd_table[write_fd].is_pipe = 1;  /* Mark as pipe */
  fd_table[write_fd].pipe_end = 1;  /* 1 = write end */

  /*
   * Return fds via kernel buffer then copy_to_user: avoids writing user memory
   * directly (required for real ring-3 callers). KERNEL_MODE debug_bins still
   * work because copy_to_user allows those addresses.
   */
  {
    int kfd[2];

    kfd[0] = read_fd;
    kfd[1] = write_fd;
    if (copy_to_user(pipefd, kfd, sizeof(kfd)) != 0)
    {
      pipe_t *pip = pipe;

      fd_table[read_fd].in_use = 0;
      fd_table[read_fd].vfs_file = NULL;
      fd_table[read_fd].is_pipe = 0;
      fd_table[read_fd].pipe_end = -1;
      fd_table[write_fd].in_use = 0;
      fd_table[write_fd].vfs_file = NULL;
      fd_table[write_fd].is_pipe = 0;
      fd_table[write_fd].pipe_end = -1;
      pipe_close_end(pip, 0);
      pipe_close_end(pip, 1);
      return -EFAULT;
    }
  }

  return 0;
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
  return (int64_t)new_brk;
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */


/* mmap flags */
#define MAP_PRIVATE 0x02
#define MAP_ANONYMOUS 0x20
#define MAP_SHARED 0x01

/* Protection flags */
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4
#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  /* addr: Hint for placement (may be ignored if MAP_FIXED not set)
   * prot: Protection flags (PROT_READ, PROT_WRITE, PROT_EXEC)
   * fd: File descriptor (only used if not MAP_ANONYMOUS)
   * offset: File offset (only used if not MAP_ANONYMOUS)
   */

  /* Debug output to serial */
  serial_print("SERIAL: mmap: entering syscall\n");

  if (!current_process)
  {
    serial_print("SERIAL: mmap: no current process\n");
    return (void *)(intptr_t)-ESRCH;
  }

  if (length == 0)
  {
    serial_print("SERIAL: mmap: zero length\n");
    return SYSCALL_PTR_ERR(EINVAL);
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    serial_print("SERIAL: mmap: invalid protection flags\n");
    return SYSCALL_PTR_ERR(EINVAL);
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
     * mmap of /dev/fb0 (FD_DEV_BASE + device_id 15 = 2015)
     * Maps framebuffer physical memory into userspace for efficient access.
     */
    if (fd >= FD_DEV_BASE && fd < FD_SYS_BASE)
    {
      uint32_t device_id = (uint32_t)(fd - FD_DEV_BASE);
#if CONFIG_ENABLE_VBE
      if (device_id == 15 && video_backend_is_available())
      {
        uint32_t fb_phys = video_backend_get_fb_phys();
        uint32_t fb_size = video_backend_get_fb_size();
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
        return (void *)virt_addr;
      }
#else
      (void)device_id;
#endif
    }
    
    /* File-based mapping not yet implemented for other files */
    serial_print("SERIAL: mmap: file-based mapping not yet implemented\n");
    return SYSCALL_PTR_ERR(ENOSYS);
  }

  /* Align length to page boundary */
  length = (length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);

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
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  /* Zero memory for anonymous mappings (as per POSIX)
   * We must switch to process page directory to write to userspace
   */
  uint64_t old_cr3 = get_current_page_directory();
  load_page_directory((uint64_t)current_process->page_directory);
  memset((void *)virt_addr, 0, length);
  load_page_directory(old_cr3);  /* Restore kernel CR3 */

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

  return (void *)virt_addr;
}

int sys_munmap(void *addr, size_t length)
{
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
        unmap_page_in_directory(current_process->page_directory, page);
      }

      /* Free the mapping structure */
      kfree(current);
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

      return 0;
    }
    current = current->next;
  }

  return -EINVAL; /* Not found */
}

/* ========================================================================== */
/* DIRECTORY OPERATIONS                                                       */
/* ========================================================================== */

int64_t sys_chdir(const char *pathname)
{
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
  int64_t ret = vfs_stat(new_path, &st);

  if (ret < 0)
    return ret;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  /* Check execute permission on directory before changing to it.
   * In Unix, you need execute permission on a directory to enter it (cd).
   */
  if (!check_file_access(new_path, ACCESS_EXEC, current_process))
  {
    return -EACCES;  /* Permission denied - no execute permission on directory */
  }

  /* Update current working directory */
  strncpy(current_process->cwd, new_path, sizeof(current_process->cwd) - 1);
  current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';

  return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
  if (!current_process || !buf || size == 0)
    return -EFAULT;

  /* Validate buffer is in userspace (for USER_MODE processes) */
  if (validate_userspace_buffer(buf, size) != 0)
    return -EFAULT;

  size_t len = strlen(current_process->cwd);
  if (len >= size)
    return -EFAULT;

  strncpy(buf, current_process->cwd, size - 1);
  buf[size - 1] = '\0';

  return len;
}

int64_t sys_unlink(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  /* Resolve relative paths against cwd */
  char resolved[256];
  const char *path_to_use = pathname;
  if (!is_absolute_path(pathname))
  {
    if (join_paths(current_process->cwd, pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }
  else
  {
    if (normalize_path(pathname, resolved, sizeof(resolved)) != 0)
      return -ENAMETOOLONG;
    path_to_use = resolved;
  }

  return vfs_unlink(path_to_use);
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
  if (sys_stat(path, &st) < 0)
    return -ENOTDIR;
  
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;
  
  /* Use VFS readdir to get directory entries */
  static struct vfs_dirent entries[32];
  int entry_count = vfs_readdir(path, entries, 32);
  
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
    dent->d_off = 0;  /* Not used in our implementation */
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
      if (sys_stat(full_path, &entry_st) >= 0)
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

  /* Register syscall interrupt handler */
  /* IDT entry 0x80 for syscalls (DPL=3 for user mode) */
  extern void syscall_entry_asm(void);  /* Assembly function (from asm), must remain extern */
  idt_set_gate64(0x80, (uint64_t)syscall_entry_asm, 0x08, 0xEE, 0);
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
WRAP3(sys_open, const char *, int, mode_t)
WRAP1(sys_close, int)
WRAP3(sys_waitpid, pid_t, int *, int)
WRAP2(sys_link, const char *, const char *)
WRAP2(sys_rename, const char *, const char *)
WRAP1(sys_unlink, const char *)
WRAP1(sys_uname, struct utsname *)
WRAP2(sys_access, const char *, int)
WRAP1(sys_dup, int)
WRAP3(sys_exec, const char *, char *const *, char *const *)
WRAP1(sys_chdir, const char *)
WRAP3(sys_mount, const char *, const char *, const char *)
WRAP2(sys_mkdir, const char *, mode_t)
WRAP1(sys_rmdir, const char *)
WRAP2(sys_chmod, const char *, mode_t)
WRAP3(sys_chown, const char *, uid_t, gid_t)
WRAP3(sys_lseek, int, off_t, int)
WRAP2(sys_getcwd, char *, size_t)
WRAP2(sys_stat, const char *, stat_t *)
WRAP2(sys_fstat, int, stat_t *)
WRAP2(sys_dup2, int, int)
WRAP1(sys_brk, void *)
WRAP6(sys_mmap, void *, size_t, int, int, int, off_t)
WRAP2(sys_munmap, void *, size_t)
WRAP3(sys_mprotect, void *, size_t, int)
WRAP2(sys_kill, pid_t, int)
WRAP3(sys_sigaction, int, const struct sigaction *, struct sigaction *)
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
  syscall_table_rw[__NR_write]          = wrap_sys_write;
  syscall_table_rw[__NR_open]           = wrap_sys_open;
  syscall_table_rw[__NR_close]          = wrap_sys_close;
  syscall_table_rw[__NR_stat]           = wrap_sys_stat;
  syscall_table_rw[__NR_fstat]          = wrap_sys_fstat;
  syscall_table_rw[__NR_poll]           = wrap_sys_poll;
  syscall_table_rw[__NR_lseek]          = wrap_sys_lseek;
  syscall_table_rw[__NR_mmap]           = wrap_sys_mmap;
  syscall_table_rw[__NR_mprotect]       = wrap_sys_mprotect;
  syscall_table_rw[__NR_munmap]         = wrap_sys_munmap;
  syscall_table_rw[__NR_brk]            = wrap_sys_brk;
  syscall_table_rw[__NR_rt_sigaction]   = wrap_sys_sigaction;
  syscall_table_rw[__NR_rt_sigreturn]   = wrap_sys_sigreturn;
  syscall_table_rw[__NR_ioctl]          = wrap_sys_ioctl;
  syscall_table_rw[__NR_pipe]           = wrap_sys_pipe;
  syscall_table_rw[__NR_dup2]           = wrap_sys_dup2;
  syscall_table_rw[__NR_nanosleep]      = wrap_sys_nanosleep;
  syscall_table_rw[__NR_getpid]         = wrap_sys_getpid;
  syscall_table_rw[__NR_getuid]         = wrap_sys_getuid;
  syscall_table_rw[__NR_geteuid]        = wrap_sys_geteuid;
  syscall_table_rw[__NR_getgid]         = wrap_sys_getgid;
  syscall_table_rw[__NR_getegid]        = wrap_sys_getegid;
  syscall_table_rw[__NR_setuid]         = wrap_sys_setuid;
  syscall_table_rw[__NR_setgid]         = wrap_sys_setgid;
  syscall_table_rw[__NR_umask]          = wrap_sys_umask;
  syscall_table_rw[__NR_sudo_auth]      = wrap_sys_sudo_auth;
  syscall_table_rw[__NR_fork]          = wrap_sys_fork;
  syscall_table_rw[__NR_execve]        = wrap_sys_exec;
  syscall_table_rw[__NR_exit]           = wrap_sys_exit;
  syscall_table_rw[__NR_wait4]          = wrap_sys_waitpid;
  syscall_table_rw[__NR_kill]           = wrap_sys_kill;
  syscall_table_rw[__NR_getdents]       = wrap_sys_getdents;
  syscall_table_rw[__NR_getcwd]         = wrap_sys_getcwd;
  syscall_table_rw[__NR_chdir]          = wrap_sys_chdir;
  syscall_table_rw[__NR_mkdir]          = wrap_sys_mkdir;
  syscall_table_rw[__NR_rmdir]          = wrap_sys_rmdir;
  syscall_table_rw[__NR_link]           = wrap_sys_link;
  syscall_table_rw[__NR_rename]         = wrap_sys_rename;
  syscall_table_rw[__NR_unlink]         = wrap_sys_unlink;
  syscall_table_rw[__NR_uname]          = wrap_sys_uname;
  syscall_table_rw[__NR_access]         = wrap_sys_access;
  syscall_table_rw[__NR_dup]            = wrap_sys_dup;
  syscall_table_rw[__NR_chmod]         = wrap_sys_chmod;
  syscall_table_rw[__NR_chown]          = wrap_sys_chown;
  syscall_table_rw[__NR_gettimeofday]   = wrap_sys_gettimeofday;
  syscall_table_rw[__NR_getppid]        = wrap_sys_getppid;
  syscall_table_rw[__NR_mount]          = wrap_sys_mount;
  syscall_table_rw[__NR_exit_group]     = wrap_sys_exit;
  syscall_table_rw[__NR_console_scroll]  = wrap_console_scroll;
  syscall_table_rw[__NR_console_clear]   = wrap_console_clear;
  syscall_table_rw[__NR_keymap_set]      = wrap_keymap_set;
  syscall_table_rw[__NR_keymap_get]      = wrap_keymap_get;

}

/* Syscall dispatcher called from assembly */
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
  if (syscall_num >= __NR_syscall_max)
    return -ENOSYS;

  syscall_handler_t handler = syscall_table_rw[syscall_num];
  return handler(arg1, arg2, arg3, arg4, arg5, arg6);
}
