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
#include <ir0/version.h>
#include <drivers/serial/serial.h>
#include <drivers/video/typewriter.h>
#include <drivers/disk/partition.h>
#include <drivers/storage/ata.h>
#include <drivers/storage/fs_types.h>
#include <fs/minix_fs.h>
#include <kernel/elf_loader.h>
#include <mm/allocator.h>
#include <mm/paging.h>
#include <ir0/kmem.h>
#include <ir0/validation.h>
#include <ir0/vga.h>
#include <ir0/stat.h>
#include <kernel/rr_sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ir0/oops.h>
#include <ir0/keyboard.h>
#include <fs/vfs.h>
#include <ir0/path.h>
#include <ir0/driver.h>
#include <drivers/audio/sound_blaster.h>
#include <drivers/IO/ps2_mouse.h>
#include <ir0/errno.h>
#include <ir0/copy_user.h>
#include <ir0/permissions.h>
#include <ir0/chmod.h>
#include <ir0/fcntl.h>
#include <ir0/procfs.h>
#include <ir0/devfs.h>
#include <ir0/signals.h>
#include <ir0/pipe.h>
#include <interrupt/arch/idt.h>
#include <mm/paging.h>
#include <fs/vfs.h>

/* Forward declarations */
static fd_entry_t *get_process_fd_table(void);
int64_t sys_unlink(const char *pathname);

static int devfs_initialized = 0;

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
  
  /* KERNEL_MODE bypass (dbgshell) */
  if (current_process->mode == KERNEL_MODE)
    return 0;
  
  /* USER_MODE: validate string is in userspace */
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
  
  /* KERNEL_MODE bypass (dbgshell) */
  if (current_process->mode == KERNEL_MODE)
    return 0;
  
  /* USER_MODE: validate buffer is in userspace */
  if (!is_user_address(buf, size))
    return -EFAULT;
  
  return 0;
}


int64_t sys_exit(int exit_code)
{
  if (!current_process)
    return -ESRCH;

  current_process->exit_code = exit_code;
  current_process->state = PROCESS_ZOMBIE;

  /* Call scheduler to switch to another process */
  rr_schedule_next();

  panicex("Process exited successfully but this should not happen in kernel mode", RUNNING_OUT_PROCESS, __FILE__, __LINE__, __func__);

  /* Should never return here */
  return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (VALIDATE_BUFFER(buf, count) != 0)
    return 0;

  /* Validate user buffer for regular files */
  char kernel_buf[PAGE_SIZE_4KB];
  const char *str = NULL;
  size_t copy_size = (count < sizeof(kernel_buf)) ? count : sizeof(kernel_buf);
  
  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
  {
    /* For console, copy from user space */
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;
    str = kernel_buf;
    uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
    /* Use typewriter VGA effect for console output */
    for (size_t i = 0; i < copy_size && i < 1024; i++)
    {
      if (str[i] == '\n')
        typewriter_vga_print("\n", color);
      else
      {
        typewriter_vga_print_char(str[i], color);
      }
    }
    return (int64_t)count;
  }

  /* Handle /dev file descriptors (special positive numbers) */
  if (fd >= 2000 && fd <= 2999)
  {
    ensure_devfs_init();
    uint32_t device_id = (uint32_t)(fd - 2000);
    devfs_node_t *node = devfs_find_node_by_id(device_id);
    if (!node || !node->ops || !node->ops->write)
      return -EBADF;
    
    /* Copy from user space for device writes */
    if (copy_from_user(kernel_buf, buf, copy_size) != 0)
      return -EFAULT;
    return node->ops->write(&node->entry, kernel_buf, copy_size, 0);
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
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
    if (ret >= 0)
    {
      return ret;
    }
    /* If pipe is full, return EPIPE or partial write */
    if (ret == -1)
      return -EPIPE; /* Broken pipe or full */
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
      fd_table[fd].offset = vfs_file->f_pos;
      return ret;
    }
    return -EIO;
  }

  return -EBADF;
}

int64_t sys_read(int fd, void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (VALIDATE_BUFFER(buf, count) != 0)
    return 0;

  /* Handle /proc file descriptors (special positive numbers) */
  if (fd >= 1000 && fd <= 1999) {
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

  /* Handle /dev file descriptors (special positive numbers) */
  if (fd >= 2000 && fd <= 2999)
  {
    char kernel_read_buf[PAGE_SIZE_4KB];
    size_t read_size = (count < sizeof(kernel_read_buf)) ? count : sizeof(kernel_read_buf);
    ensure_devfs_init();
    uint32_t device_id = (uint32_t)(fd - 2000);
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

  if (fd == STDIN_FILENO)
  {
    /* Read from keyboard buffer - NON-BLOCKING */
    char kernel_read_buf[256];
    size_t bytes_read = 0;

    /* Only read if there's data available */
    if (keyboard_buffer_has_data())
    {
      char c = keyboard_buffer_get();
      if (c != 0)
      {
        kernel_read_buf[bytes_read++] = c;
      }
    }

    /* Copy to user space */
    if (bytes_read > 0)
    {
      size_t copy_len = (bytes_read < count) ? bytes_read : count;
      if (copy_to_user(buf, kernel_read_buf, copy_len) != 0)
        return -EFAULT;
      return (int64_t)copy_len;
    }

    /* Return 0 if no data available */
    return 0;
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
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
      fd_table[fd].offset = vfs_file->f_pos;
      return ret;
    }
    return -EIO;
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
  /* No parent tracking yet */
  return 0;
}

int64_t sys_mkdir(const char *pathname, mode_t mode)
{
  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (current_process->mode == USER_MODE && !is_user_address(pathname, 256))
  {
    return -EFAULT;
  }

  /* Use VFS layer with proper mode */
  return vfs_mkdir(pathname, (int)mode);
}

int64_t sys_exec(const char *pathname,
                 char *const argv[] __attribute__((unused)),
                 char *const envp[] __attribute__((unused)))
{
  serial_print("SERIAL: sys_exec called\n");

  if (!current_process || !pathname)
  {
    return -EFAULT;
  }

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (current_process->mode == USER_MODE && !is_user_address(pathname, 256))
  {
    return -EFAULT;
  }

  /* Load and execute ELF binary using kernel-level exec
   * This replaces the current process image with the new ELF binary.
   * Currently simple implementation - full ELF support with sections,
   * dynamic linking, and proper process setup would be added later.
   */
  return kexecve(pathname);
}

int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype)
{
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

  /* Validate device path */
  if (dev[0] != '/' || strlen(dev) >= 256) {
    sys_write(STDERR_FILENO, "mount: invalid device path\n", 26);
    return -EFAULT;
  }

  /* Validate mountpoint path */
  if (mountpoint[0] != '/' || strlen(mountpoint) >= 256) {
    sys_write(STDERR_FILENO, "mount: invalid mount point\n", 26);
    return -EFAULT;
  }

  /* Check if mountpoint exists and is a directory */
  stat_t st;
  if (vfs_stat(mountpoint, &st) < 0) {
    sys_write(STDERR_FILENO, "mount: mount point does not exist\n", 33);
    return -EFAULT;
  }
  if (!S_ISDIR(st.st_mode)) {
    sys_write(STDERR_FILENO, "mount: mount point is not a directory\n", 37);
    return -EFAULT;
  }

  /* Use unified VFS mount; fstype may be NULL to autodetect */
  int ret = vfs_mount(dev, mountpoint, fstype);
  if (ret < 0) {
    /* Report specific error */
    if (!fstype || !*fstype) {
      sys_write(STDERR_FILENO, "mount: failed to autodetect filesystem type\n", 43);
    } else {
      sys_write(STDERR_FILENO, "mount: failed to mount ", 22);
      sys_write(STDERR_FILENO, fstype, strlen(fstype));
      sys_write(STDERR_FILENO, " filesystem\n", 12);
    }
    return -1;
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

  /* Call chmod through VFS layer */
  return chmod(path, mode);
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
 * sys_creat - Create file (POSIX syscall, but deprecated in favor of open)
 * @pathname: Path to file to create
 * @mode: File permissions
 *
 * NOTE: This is a POSIX syscall, but modern code should use:
 *   open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
 *
 * POSIX specifies creat() as equivalent to open() with O_CREAT | O_WRONLY | O_TRUNC.
 * We maintain this for POSIX compatibility, but open() is preferred.
 *
 * Returns: File descriptor on success, negative error code on failure
 */
int64_t sys_creat(const char *pathname, mode_t mode)
{
  if (!current_process || !pathname)
    return -EFAULT;

  /* POSIX-compatible: creat() is equivalent to open(O_CREAT | O_WRONLY | O_TRUNC) */
  return sys_open(pathname, O_CREAT | O_WRONLY | O_TRUNC, mode);
}

/**
 * sys_rmdir - Remove directory (POSIX)
 * @pathname: Path to directory to remove
 *
 * POSIX-compliant rmdir syscall. Uses VFS layer for filesystem abstraction.
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

  /* Use VFS layer for POSIX-compliant directory removal */
  return vfs_rmdir_recursive(pathname);
}

static fd_entry_t *get_process_fd_table(void)
{
  if (!current_process)
  {
    return NULL;
  }

  static bool initialized = false;
  if (!initialized)
  {
    process_init_fd_table(current_process);
    initialized = true;
  }

  return current_process->fd_table;
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

  (void)mode; /* Mode handling not implemented yet */

  /* Handle /proc filesystem on-demand */
  if (is_proc_path(pathname)) {
    return proc_open(pathname, flags);
  }

  /* Handle /dev filesystem on-demand */
  if (is_dev_path(pathname))
  {
    ensure_devfs_init();
    devfs_node_t *node = devfs_find_node(pathname);
    if (!node)
      return -ENOENT;
    return 2000 + (int64_t)node->entry.device_id;
  }

  /* Check access permissions based on flags */
  int access_mode = 0;
  if (flags & O_RDONLY || flags & O_RDWR)
    access_mode |= ACCESS_READ;
  if (flags & O_WRONLY || flags & O_RDWR)
    access_mode |= ACCESS_WRITE;
  
  if (access_mode && !check_file_access(pathname, access_mode, current_process))
    return -EACCES;

  fd_entry_t *fd_table = get_process_fd_table();

  /* Find free file descriptor */
  int fd = -1;
  for (int i = 3; i < MAX_FDS_PER_PROCESS;
       i++)
  {
    /* Start from 3 (after stdin/stdout/stderr) */
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }

  if (fd == -1)
    /* Too many open files */
    return -EMFILE;

  /* Open file using VFS layer to get real file handle */
  struct vfs_file *vfs_file = NULL;
  int ret = vfs_open(pathname, flags, &vfs_file);
  if (ret != 0)
  {
    return -ENOENT;
  }

  /* Set up file descriptor with real VFS file handle */
  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, pathname, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = flags;
  fd_table[fd].vfs_file = vfs_file;
  fd_table[fd].offset = vfs_file ? vfs_file->f_pos : 0;

  return fd;
}

int64_t sys_close(int fd)
{
  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();

  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd <= 2)
    return -EBADF;

  if (fd >= 2000 && fd <= 2999)
    return 0;

  /* Check if this is a pipe */
  if (fd_table[fd].is_pipe && fd_table[fd].vfs_file)
  {
    /* Close pipe end - this decrements ref_count */
    pipe_t *pipe = (pipe_t *)fd_table[fd].vfs_file;
    pipe_close(pipe);
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

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  off_t new_offset;

  if (fd <= 2)
  {
    if (whence == 0)
      new_offset = offset;
    else if (whence == 1)
      new_offset = fd_table[fd].offset + offset;
    else
      return -ESPIPE;
  }
  else
  {
    /* Use VFS file handle if available for better offset management */
    if (fd_table[fd].vfs_file)
    {
      struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
      if (vfs_file->f_inode && vfs_file->f_inode->i_fop && 
          vfs_file->f_inode->i_fop->seek)
      {
        /* Use filesystem's seek implementation */
        off_t result = vfs_file->f_inode->i_fop->seek(vfs_file, offset, whence);
        if (result < 0)
          return result;
        new_offset = result;
        fd_table[fd].offset = new_offset;
      }
      else
      {
        /* Fallback to stat-based calculation */
        stat_t st;
        if (vfs_stat(fd_table[fd].path, &st) != 0)
          return -EBADF;

        switch (whence)
        {
        case 0:
          new_offset = offset;
          break;
        case 1:
          new_offset = fd_table[fd].offset + offset;
          break;
        case 2:
          new_offset = st.st_size + offset;
          break;
        default:
          return -EINVAL;
        }
        fd_table[fd].offset = new_offset;
      }
    }
    else
    {
      /* Fallback to stat-based calculation */
      stat_t st;
      if (vfs_stat(fd_table[fd].path, &st) != 0)
        return -EBADF;

      switch (whence)
      {
      case 0:
        new_offset = offset;
        break;
      case 1:
        new_offset = fd_table[fd].offset + offset;
        break;
      case 2:
        new_offset = st.st_size + offset;
        break;
      default:
        return -EINVAL;
      }
      fd_table[fd].offset = new_offset;
    }
  }

  if (new_offset < 0)
    return -EINVAL;

  fd_table[fd].offset = new_offset;
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

  if (fd_table[newfd].in_use && newfd > 2)
  {
    fd_table[newfd].in_use = false;
    fd_table[newfd].path[0] = '\0';
    fd_table[newfd].flags = 0;
    fd_table[newfd].offset = 0;
  }

  fd_table[newfd].in_use = true;
  strncpy(fd_table[newfd].path, fd_table[oldfd].path, sizeof(fd_table[newfd].path) - 1);
  fd_table[newfd].path[sizeof(fd_table[newfd].path) - 1] = '\0';
  fd_table[newfd].flags = fd_table[oldfd].flags;
  fd_table[newfd].offset = fd_table[oldfd].offset;
  fd_table[newfd].vfs_file = fd_table[oldfd].vfs_file;

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

  /* Use VFS layer instead of direct MINIX calls */
  return vfs_stat(pathname, buf);
}

/**
 * sys_fork - Fork process (POSIX fork syscall)
 * 
 * NOTE: IR0 only uses spawn() for process creation.
 * This syscall exists for POSIX compatibility but uses spawn() internally.
 * 
 * Returns: Child PID in parent, 0 in child (via process_fork)
 */
int64_t sys_fork(void)
{
  if (!current_process)
    return -ESRCH;

  /* IR0 philosophy: only spawn() creates processes
   * process_fork() uses spawn() internally for syscall compatibility */
  return process_fork();
}


int64_t sys_wait4(pid_t pid, int *status, int options, void *rusage)
{
  (void)options;
  (void)rusage;

  if (!current_process)
    return -ESRCH;

  return process_wait(pid, status);
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
    pipe_close(pipe); /* Clean up pipe on failure */
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

  /* Return file descriptors to userspace */
  pipefd[0] = read_fd;
  pipefd[1] = write_fd;

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
    /* Start heap at 32MB (0x2000000) - after code/stack */
    current_process->heap_start = 0x2000000UL;
    current_process->heap_end = current_process->heap_start;
    current_brk = current_process->heap_end;
  }

  /* Validate new break is within reasonable range */
  if (new_brk < current_process->heap_start ||
      new_brk > (current_process->heap_start + 0x10000000))  /* 256MB max heap */
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
      extern int map_user_region_in_directory(uint64_t *pml4, uintptr_t virtual_start, size_t size, uint64_t flags);
      if (map_user_region_in_directory(current_process->page_directory, start_page, size_to_map, PAGE_RW) != 0)
      {
        /* Failed to map - return current break */
        return (int64_t)current_process->heap_end;
      }
    }
  }
  /* If shrinking heap, unmap pages (optional - for now just update break) */
  else if (new_brk < current_brk)
  {
    /* TODO: Unmap pages if needed (for now just update break) */
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

/* Simple memory mapping structure */
struct mmap_region
{
  void *addr;              /* Virtual address (actual or hint) */
  void *hint_addr;         /* Requested hint address (for tracking) */
  size_t length;
  int prot;
  int flags;
  struct mmap_region *next;
};

static struct mmap_region *mmap_list = NULL;

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
               off_t offset)
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
    return (void *)-1;
  }

  if (length == 0)
  {
    serial_print("SERIAL: mmap: zero length\n");
    return (void *)-1;
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    serial_print("SERIAL: mmap: invalid protection flags\n");
    return (void *)-1;
  }

  /* Validate offset alignment for file mappings */
  if (!(flags & MAP_ANONYMOUS))
  {
    if (fd < 0)
    {
      serial_print("SERIAL: mmap: file mapping requires valid fd\n");
      return (void *)-1;
    }
    
    /* Offset must be page-aligned for file mappings */
    if (offset % PAGE_SIZE_4KB != 0)
    {
      serial_print("SERIAL: mmap: offset must be page-aligned\n");
      return (void *)-1;
    }
    
    /* File-based mapping not yet fully implemented */
    /* For now, still only support anonymous mappings */
    serial_print("SERIAL: mmap: file-based mapping not yet implemented\n");
    return (void *)-1;
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
      return (void *)-1;
    }
    
    /* Check if hint is in valid userspace range */
    if (!is_user_address(addr, length))
    {
      serial_print("SERIAL: mmap: address hint not in userspace\n");
      return (void *)-1;
    }
    
    /* Check if address range is already mapped */
    int mapped = is_page_mapped_in_directory(current_process->page_directory, hint_addr, NULL);
    if (mapped == 1)
    {
      serial_print("SERIAL: mmap: address range already mapped\n");
      return (void *)-1;  /* Address already in use */
    }
    
    virt_addr = hint_addr;
  }
  else
  {
    /* Kernel chooses address - start from 128MB (after heap) */
    virt_addr = 0x8000000UL;  /* 128MB */
    /* TODO: Find unused address range (for now use fixed start) */
  }

  /* Determine page flags from protection flags */
  uint64_t page_flags = PAGE_USER;  /* Always user mode */
  if (prot & PROT_READ)
    page_flags |= 0;  /* Read is default */
  if (prot & PROT_WRITE)
    page_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    page_flags |= 0;  /* TODO: Add PAGE_EXEC flag if needed */

  /* Map pages in process page directory */
  if (map_user_region_in_directory(current_process->page_directory, virt_addr, length, page_flags) != 0)
  {
    serial_print("SERIAL: mmap: failed to map pages\n");
    return (void *)-1;
  }

  /* Zero memory for anonymous mappings (as per POSIX)
   * We must switch to process page directory to write to userspace
   */
  uint64_t old_cr3 = get_current_page_directory();
  load_page_directory((uint64_t)current_process->page_directory);
  memset((void *)virt_addr, 0, length);
  load_page_directory(old_cr3);  /* Restore kernel CR3 */

  /* Create mapping entry */
  struct mmap_region *region = kmalloc(sizeof(struct mmap_region));
  if (!region)
  {
      /* Failed to allocate region entry - unmap pages */
      for (uintptr_t page = virt_addr; page < virt_addr + length; page += PAGE_SIZE_4KB)
    {
      unmap_page(page);
    }
    return (void *)-1;
  }

  region->addr = (void *)virt_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = mmap_list;
  mmap_list = region;

  return (void *)virt_addr;
}

int sys_munmap(void *addr, size_t length)
{
  if (!current_process || !addr || length == 0)
    return -1;

  /* Validate address is in userspace */
  if (!is_user_address(addr, length))
    return -EFAULT;

  /* Align to page boundaries */
  uintptr_t start_page = (uintptr_t)addr & ~0xFFF;
  size_t aligned_length = ((length + 0xFFF) & ~0xFFF);

  /* Find the mapping */
  struct mmap_region *current = mmap_list;
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
        mmap_list = current->next;

      /* Unmap pages in process page directory */
      for (uintptr_t page = start_page; page < start_page + aligned_length; page += PAGE_SIZE_4KB)
      {
        unmap_page(page);
      }

      /* Free the mapping structure */
      kfree(current);
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -1; /* Not found */
}

int sys_mprotect(void *addr, size_t len, int prot)
{
  if (!current_process || !addr || len == 0)
    return -1;

  /* Find the mapping */
  struct mmap_region *current = mmap_list;
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
      /* Update protection */
      current->prot = prot;
      return 0;
    }
    current = current->next;
  }

  return -1; // Not found
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
    return -EFAULT;

  /* Calculate new path */
  char new_path[256];

  if (is_absolute_path(pathname)) {
    /* Absolute path - just normalize it */
    if (normalize_path(pathname, new_path, sizeof(new_path)) != 0)
      return -EFAULT;
  } else {
    /* Relative path - join with current working directory */
    if (join_paths(current_process->cwd, pathname, new_path, sizeof(new_path)) != 0)
      return -EFAULT;
  }

  /* Verify directory exists */
  stat_t st;
  int64_t ret = vfs_stat(new_path, &st);
  if (ret < 0 || !S_ISDIR(st.st_mode))
    return -EFAULT;

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

  /* Call VFS unlink function */
  return vfs_unlink(pathname);
}

void syscalls_init(void)
{
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
  idt_set_gate64(0x80, (uint64_t)syscall_entry_asm, 0x08, 0xEE);
}

/* Syscall dispatcher called from assembly */
/**
 * syscall_dispatch - Dispatch system call to appropriate handler
 * @syscall_num: System call number (from syscall_num_t enum)
 * @arg1-arg5: System call arguments
 *
 * This function routes POSIX-compliant system calls to their handlers.
 * Uses enum values instead of hardcoded numbers for type safety and clarity.
 *
 * Returns: System call return value, or -ENOSYS for unknown syscall
 */
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5)
{
  /* Use enum values for type safety - compiler will catch typos */
  switch (syscall_num)
  {
  case SYS_EXIT:
    return sys_exit((int)arg1);
  case SYS_FORK:
    return sys_fork();
  case SYS_READ:
    return sys_read((int)arg1, (void *)arg2, (size_t)arg3);
  case SYS_WRITE:
    return sys_write((int)arg1, (const void *)arg2, (size_t)arg3);
  case SYS_OPEN:
    return sys_open((const char *)arg1, (int)arg2, (mode_t)arg3);
  case SYS_CLOSE:
    return sys_close((int)arg1);
  case SYS_WAITPID:
    return sys_waitpid((pid_t)arg1, (int *)arg2, (int)arg3);
  case SYS_CREAT:
    return sys_creat((const char *)arg1, (mode_t)arg2);
  case SYS_LINK:
    return sys_link((const char *)arg1, (const char *)arg2);
  case SYS_UNLINK:
    return sys_unlink((const char *)arg1);
  case SYS_EXEC:
    return sys_exec((const char *)arg1, (char *const *)arg2, (char *const *)arg3);
  case SYS_CHDIR:
    return sys_chdir((const char *)arg1);
  case SYS_GETPID:
    return sys_getpid();
  case SYS_MOUNT:
    return sys_mount((const char *)arg1, (const char *)arg2, (const char *)arg3);
  case SYS_MKDIR:
    return sys_mkdir((const char *)arg1, (mode_t)arg2);
  case SYS_RMDIR:
    return sys_rmdir((const char *)arg1);
  case SYS_CHMOD:
    return sys_chmod((const char *)arg1, (mode_t)arg2);
  case SYS_LSEEK:
    return sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
  case SYS_GETCWD:
    return sys_getcwd((char *)arg1, (size_t)arg2);
  case SYS_STAT:
    return sys_stat((const char *)arg1, (stat_t *)arg2);
  case SYS_FSTAT:
    return sys_fstat((int)arg1, (stat_t *)arg2);
  case SYS_DUP2:
    return sys_dup2((int)arg1, (int)arg2);
  case SYS_BRK:
    return sys_brk((void *)arg1);
  case SYS_MMAP:
    return (int64_t)sys_mmap((void *)arg1, (size_t)arg2, (int)arg3, (int)arg4,
                             (int)arg5, (off_t)0);
  case SYS_MUNMAP:
    return sys_munmap((void *)arg1, (size_t)arg2);
  case SYS_MPROTECT:
    return sys_mprotect((void *)arg1, (size_t)arg2, (int)arg3);
  case SYS_GETPPID:
    return sys_getppid();
  case SYS_KILL:
    return sys_kill((pid_t)arg1, (int)arg2);
  case SYS_SIGACTION:
    return sys_sigaction((int)arg1, (const struct sigaction *)arg2, (struct sigaction *)arg3);
  case SYS_PIPE:
    return sys_pipe((int *)arg1);
  case SYS_SIGRETURN:
    return sys_sigreturn((struct sigcontext *)arg1);
  default:
    /* Unknown syscall - return ENOSYS (function not implemented) */
    return -ENOSYS;
  }
}
