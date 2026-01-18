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
#include <ir0/fcntl.h>
#include <ir0/procfs.h>
#include <ir0/devfs.h>
#include <ir0/signals.h>

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

  /* Call chmod through VFS layer */
  extern int chmod(const char *path, mode_t mode);
  return chmod(path, mode);
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
  if (!current_process || !oldpath || !newpath)
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
  (void)mode; /* Mode handling not implemented yet */

  if (!current_process || !pathname)
    return -EFAULT;

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

  /* Close VFS file if it exists */
  if (fd_table[fd].vfs_file)
  {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    vfs_close(vfs_file);
    fd_table[fd].vfs_file = NULL;
  }

  fd_table[fd].in_use = false;
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

  /* Handle /proc filesystem */
  if (is_proc_path(pathname)) {
    return proc_stat(pathname, buf);
  }

  /* Use VFS layer instead of direct MINIX calls */
  return vfs_stat(pathname, buf);
}

int64_t sys_fork(void)
{
  if (!current_process)
    return -ESRCH;

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

int64_t sys_brk(void *addr)
{
  if (!current_process)
    return -ESRCH;

  /* If addr is NULL, return current break */
  if (!addr)
    return (int64_t)current_process->heap_end;

  /* Validate new break address */
  if (addr < (void *)current_process->heap_start ||
      addr > (void *)((char *)current_process->heap_start + 0x10000000))
    return -EFAULT;

  /* Set new break */
  current_process->heap_end = (uint64_t)addr;
  return (int64_t)addr;
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

  sys_write(1, "mmap: allocating memory\n", 24);

  /* Align length to page boundary */
  length = (length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);

  /* Address hint support:
   * - If addr is NULL: Kernel chooses address
   * - If addr is provided: Try to use it if valid and page-aligned
   * - If MAP_FIXED is set (future): Must use exact address
   * For now, we honor hints but allow kernel to override
   */
  void *real_addr = NULL;
  
  /* Check if hint address is provided and valid */
  if (addr != NULL)
  {
    /* Address must be page-aligned */
    uintptr_t hint_addr = (uintptr_t)addr;
    if ((hint_addr & (PAGE_SIZE_4KB - 1)) == 0)
    {
      /* Check if hint is in reasonable range (user space: 4MB to 3GB) */
      const uintptr_t USER_SPACE_START = 0x00400000UL;  /* 4MB */
      const uintptr_t USER_SPACE_END   = 0xC0000000UL;  /* 3GB */
      
      if (hint_addr >= USER_SPACE_START && hint_addr < USER_SPACE_END)
      {
        /* Hint is valid - try to allocate near the hint address
         * Store hint in mapping entry for tracking
         * Note: Full implementation would actually map at hint_addr in page tables
         */
        real_addr = kmalloc(length);
        if (real_addr)
        {
          /* Store hint address in mapping entry for future page table mapping
           * In a full implementation, we would:
           * 1. Allocate physical frame
           * 2. Map it at hint_addr in process page tables
           * 3. Store mapping entry with hint_addr as virtual address
           * For now, we use allocated address but document the hint
           */
        }
      }
    }
  }
  
  /* If no hint or hint was invalid, kernel chooses */
  if (!real_addr)
  {
    real_addr = kmalloc(length);
  }
  
  if (!real_addr)
  {
    return (void *)-1;
  }
  
  /* Zero memory for anonymous mappings (as per POSIX) */
  memset(real_addr, 0, length);

  /* Create mapping entry */
  struct mmap_region *region = kmalloc(sizeof(struct mmap_region));
  if (!region)
  {
    kfree(real_addr);
    return (void *)-1;
  }

  region->addr = real_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = mmap_list;
  mmap_list = region;

  return real_addr;
}

int sys_munmap(void *addr, size_t length)
{
  if (!current_process || !addr || length == 0)
    return -1;

  /* Find the mapping */
  struct mmap_region *current = mmap_list;
  struct mmap_region *prev = NULL;

  while (current)
  {
    if (current->addr == addr && current->length == length)
    {
      /* Remove from list */
      if (prev)
        prev->next = current->next;
      else
        mmap_list = current->next;

      /* Free the mapping structure */
      kfree(current);
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -1; // Not found
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

  /* Update current working directory */
  strncpy(current_process->cwd, new_path, sizeof(current_process->cwd) - 1);
  current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';

  return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
  if (!current_process || !buf || size == 0)
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
  if (!pathname)
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
  extern void syscall_entry_asm(void);
  extern void idt_set_gate64(uint8_t num, uint64_t base, uint16_t sel,
                             uint8_t flags);

  /* IDT entry 0x80 for syscalls (DPL=3 for user mode) */
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
  default:
    /* Unknown syscall - return ENOSYS (function not implemented) */
    return -ENOSYS;
  }
}
