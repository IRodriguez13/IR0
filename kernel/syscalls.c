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
#include <drivers/serial/serial.h>
#include <drivers/video/typewriter.h>
#include <drivers/disk/partition.h>
#include <drivers/storage/ata.h>
#include <drivers/storage/fs_types.h>
#include <fs/minix_fs.h>
#include <kernel/elf_loader.h>
#include <ir0/memory/allocator.h>
#include <ir0/memory/kmem.h>
#include <ir0/vga.h>
#include <ir0/net.h>
#include <net/icmp.h>
#include <net/ip.h>
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

/* Forward declaration */
static fd_entry_t *get_process_fd_table(void);

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

  /* Llamar al scheduler para pasar a otro proceso */
  rr_schedule_next();

  panicex("You left the shell succesfully! but you should't do that!", RUNNING_OUT_PROCESS, "SYSCALLS.C", 75, "sys_exit");

  /* Nunca debería volver aquí */
  return 0;
}

int64_t sys_write(int fd, const void *buf, size_t count)
{
  if (!current_process)
    return -ESRCH;
  if (!buf || count == 0)
    return 0;

  if (fd == STDOUT_FILENO || fd == STDERR_FILENO)
  {
    const char *str = (const char *)buf;
    uint8_t color = (fd == STDERR_FILENO) ? 0x0C : 0x0F;
    /* Use typewriter VGA effect for console output */
    for (size_t i = 0; i < count && i < 1024; i++)
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
    return node->ops->write(&node->entry, buf, count, 0);
  }

  /* Handle regular file descriptors */
  fd_entry_t *fd_table = get_process_fd_table();
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
    return -EBADF;

  /* Check write permissions */
  if (!check_file_access(fd_table[fd].path, ACCESS_WRITE, current_process))
    return -EACCES;

  /* Use VFS file handle if available */
  if (fd_table[fd].vfs_file)
  {
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    int ret = vfs_write(vfs_file, (const char *)buf, count);
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
  if (!buf || count == 0)
    return 0;

  /* Handle /proc file descriptors (special positive numbers) */
  if (fd >= 1000 && fd <= 1999) {
    return proc_read(fd, (char*)buf, count);
  }

  /* Handle /dev file descriptors (special positive numbers) */
  if (fd >= 2000 && fd <= 2999)
  {
    ensure_devfs_init();
    uint32_t device_id = (uint32_t)(fd - 2000);
    devfs_node_t *node = devfs_find_node_by_id(device_id);
    if (!node || !node->ops || !node->ops->read)
      return -EBADF;
    return node->ops->read(&node->entry, buf, count, 0);
  }

  if (fd == STDIN_FILENO)
  {
    /* Read from keyboard buffer - NON-BLOCKING */
    char *buffer = (char *)buf;
    size_t bytes_read = 0;

    /* Only read if there's data available */
    if (keyboard_buffer_has_data())
    {
      char c = keyboard_buffer_get();
      if (c != 0)
      {
        buffer[bytes_read++] = c;
      }
    }

    return (int64_t)bytes_read; // Return 0 if no data available
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
    struct vfs_file *vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
    int ret = vfs_read(vfs_file, (char *)buf, count);
    if (ret >= 0)
    {
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
  return 0; // No parent tracking yet
}

int64_t sys_ls(const char *pathname)
{
  if (!current_process)
    return -ESRCH;

  /* Use VFS layer for better abstraction */
  const char *target_path = pathname ? pathname : "/";
  return vfs_ls(target_path);
}

/* Enhanced ls with detailed file information (like Linux ls -l) */
int64_t sys_ls_detailed(const char *pathname)
{
  if (!current_process)
    return -ESRCH;

  const char *target_path = pathname ? pathname : "/";

  /* First get directory listing, then stat each file */
  return vfs_ls_with_stat(target_path);
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

int64_t sys_ps(void)
{
  /* Use /proc filesystem - show current process status */
  // For now, show current process via /proc/status
  int fd = sys_open("/proc/status", O_RDONLY, 0);
  if (fd < 0) {
    return -1;
  }
  
  char buffer[1024];
  int64_t bytes = sys_read(fd, buffer, sizeof(buffer) - 1);
  sys_close(fd);
  
  if (bytes > 0) {
    sys_write(STDOUT_FILENO, buffer, bytes);
    sys_write(STDOUT_FILENO, "\n", 1);
  }
  
  return 0;
}

int64_t sys_touch(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_touch(pathname, 0644);  // Default mode

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}


int64_t sys_write_file(const char *pathname, const char *content)
{
  if (!current_process || !pathname || !content)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_write_file(pathname, content);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
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

  /* For now, simple implementation - load and execute ELF */
  return elf_load_and_execute(pathname);
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

/* Get current user information */
int64_t sys_whoami(void) {
  if (!current_process)
    return -ESRCH;

  /* Use new permission system - simple root/user display */
  const char *username = (current_process->uid == ROOT_UID) ? "root" : "user";
  
  /* Print username */
  sys_write(STDOUT_FILENO, username, strlen(username));
  sys_write(STDOUT_FILENO, "\n", 1);

  return 0;
}

int64_t sys_chmod(const char *path, mode_t mode) 
{
  if (!current_process || !path)
    return -EFAULT;

  /* Call chmod through VFS layer */
  extern int chmod(const char *path, mode_t mode);
  return chmod(path, mode);
}

int64_t sys_append(const char *path, const char *content, size_t count)
{
  if (!current_process || !path || !content)
    return -EFAULT;

  /* Call append through VFS layer */
  extern int vfs_append(const char *path, const char *content, size_t count);
  return vfs_append(path, content, count);
}

int64_t sys_df(void)
{
  if (!current_process)
    return -ESRCH;

  typewriter_vga_print("Filesystem          Size\n", 0x0F);
  typewriter_vga_print("----------------------------------\n", 0x07);

  int found_drives = 0;
  for (uint8_t i = 0; i < 4; i++)
  {
    if (!ata_drive_present(i))
      continue;

    found_drives++;
    char devname[16];
    int len = snprintf(devname, sizeof(devname), "/dev/hd%c", 'a' + i);
    if (len < 0 || len >= (int)sizeof(devname))
      continue;

    /* ata_get_size() returns size in 512-byte sectors */
    uint64_t size = ata_get_size(i);
    if (size == 0)
    {
      char line[64];
      snprintf(line, sizeof(line), "%-20s (empty)\n", devname);
      typewriter_vga_print(line, 0x0E);
      continue;
    }

    char size_str[32];
    /* Same calculation as lsblk: sectors / (2 * 1024 * 1024) = GB */
    uint64_t size_gb = size / (2 * 1024 * 1024);
    if (size_gb > 0)
    {
      len = snprintf(size_str, sizeof(size_str), "%lluG", size_gb);
    }
    else
    {
      /* sectors / (2 * 1024) = MB */
      uint64_t size_mb = size / (2 * 1024);
      len = snprintf(size_str, sizeof(size_str), "%lluM", size_mb);
    }

    if (len > 0 && len < (int)sizeof(size_str))
    {
      char line[64];
      snprintf(line, sizeof(line), "%-20s %s\n", devname, size_str);
      typewriter_vga_print(line, 0x0F);
    }
  }

  if (found_drives == 0)
  {
    typewriter_vga_print("No drives detected\n", 0x0E);
  }

  return 0;
}

int64_t sys_link(const char *oldpath, const char *newpath)
{
  if (!current_process || !oldpath || !newpath)
    return -EFAULT;

  /* Call link through VFS layer */
  return vfs_link(oldpath, newpath);
}

int64_t sys_lsblk(void)
{
  if (!current_process)
    return -ESRCH;
    
  uint8_t drives[4] = {0};
  int drive_count = 0;
  
  for (uint8_t i = 0; i < 4; i++) {
    if (ata_drive_present(i)) {
      drives[drive_count++] = i;
    }
  }

  sys_write(1, "NAME MAJ:MIN SIZE MODEL\n", 23);

  /* For each drive */
  for (int i = 0; i < drive_count; i++) {
    uint8_t drive = drives[i];
    char info[256];
    int len = 0;
    
    uint64_t size = ata_get_size(drive);
    const char *model = ata_get_model(drive);
    const char *serial = ata_get_serial(drive);
    
    /* Format basic info */
    len = snprintf(info, sizeof(info), "hd%c  %3d:0   %5lluG %s (%s)\n", 
                   'a' + drive, drive, size / (2 * 1024 * 1024), model, serial);
    
    sys_write(1, info, len);

    /* Read first sector to check partition table type */
    uint8_t first_sector[512];
    if (ata_read_sectors(drive, 0, 1, first_sector) == 0) {
      /* Detect partition table type and handle accordingly */
      if (first_sector[450] == 0xEE) { // GPT signature check
        /* GPT handling will be implemented later */
        continue;
      }

      /* Check MBR signature */
      if (first_sector[510] == 0x55 && first_sector[511] == 0xAA) {
        /* Process MBR partition entries */
        for (int j = 0; j < 4; j++) {
          /* Partition entry offset calculation */
          int entry_offset = 446 + (j * 16);
          uint8_t system_id = first_sector[entry_offset + 4];
          uint32_t total_sectors;

          memcpy(&total_sectors, &first_sector[entry_offset + 12], 4);

          if (system_id != 0) {
            len = snprintf(info, sizeof(info), "└─hd%c%d %3d:%-3d %5uG %s\n",
                         'a' + drive, j + 1, drive, j + 1,
                         total_sectors / (2 * 1024 * 1024),
                         get_fs_type(system_id));
            sys_write(1, info, len);
          }
        }
      }
    }
  }

  return 0;
}

int64_t sys_creat(const char *pathname, mode_t mode)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_touch(pathname, mode);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rm(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rm(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rmdir(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
    return minix_fs_rmdir(pathname);

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
}

int64_t sys_rmdir_force(const char *pathname)
{
  if (!current_process || !pathname)
    return -EFAULT;

  if (minix_fs_is_working())
  {
    extern int minix_fs_rmdir_force(const char *path);
    return minix_fs_rmdir_force(pathname);
  }

  sys_write(STDERR_FILENO, "Error: filesystem not ready\n", 29);
  return -1;
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
  { // Start from 3 (after stdin/stdout/stderr)
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }

  if (fd == -1)
    return -EMFILE; // Too many open files

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

/* Spawn a new process with specific entry point */
int64_t sys_spawn(void (*entry)(void), const char *name)
{
  if (!current_process || !entry || !name)
    return -EFAULT;

  return process_spawn(entry, name);
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

int64_t sys_kernel_info(void *info_buffer, size_t buffer_size)
{
  if (!current_process || !info_buffer)
    return -EFAULT;

  const char *info = "IR0 Kernel v0.0.1 x86-64\n";
  size_t len = 26; // Length of info string

  if (buffer_size < len)
    len = buffer_size;

  /* Simple copy without memcpy dependency */
  char *dst = (char *)info_buffer;
  for (size_t i = 0; i < len; i++)
    dst[i] = info[i];

  return (int64_t)len;
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

void *sys_sbrk(intptr_t increment)
{
  if (!current_process)
    return (void *)-1;

  void *old_break = (void *)current_process->heap_end;
  void *new_break = (char *)old_break + increment;

  /* Check bounds (simplified) */
  if (new_break < (void *)current_process->heap_start ||
      new_break > (void *)((char *)current_process->heap_start + 0x10000000))
    return (void *)-1;

  /* Update break */
  current_process->heap_end = (uint64_t)new_break;
  return old_break;
}

/* ============================================================================ */
/* MEMORY MAPPING SYSCALLS (mmap/munmap) */
/* ============================================================================ */

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
  void *addr;
  size_t length;
  int prot;
  int flags;
  struct mmap_region *next;
};

static struct mmap_region *mmap_list = NULL;

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd,
               off_t offset)
{
  (void)addr;
  (void)prot;
  (void)fd;
  (void)offset; // Ignore for now

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

  /* Debug: show what flags we received */
  serial_print("SERIAL: mmap: flags received = ");
  serial_print_hex32((uint32_t)flags);
  serial_print("\n");

  /* Only support anonymous mapping for now */
  if (!(flags & MAP_ANONYMOUS))
  {
    serial_print("SERIAL: mmap: not anonymous mapping\n");
    serial_print("SERIAL: mmap: MAP_ANONYMOUS = 0x20\n");
    return (void *)-1;
  }

  sys_write(1, "mmap: allocating memory\n", 24);

  /* Align length to reasonable boundary */
  length = (length + 15) & ~15;

  /* For simplicity, use kernel allocator to get real memory */
  void *real_addr = kmalloc(length);
  if (!real_addr)
  {
    return (void *)-1;
  }

  /* Create mapping entry */
  struct mmap_region *region = kmalloc(sizeof(struct mmap_region));
  if (!region)
  {
    kfree(real_addr);
    return (void *)-1;
  }

  region->addr = real_addr;
  region->length = length;
  region->prot = prot;
  region->flags = flags;
  region->next = mmap_list;
  mmap_list = region;

  /* Zero the memory if it's anonymous */
  if (flags & MAP_ANONYMOUS)
  {
    for (size_t i = 0; i < length; i++)
      ((char *)real_addr)[i] = 0;
  }

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

int64_t sys_rmdir_recursive(const char *pathname)
{
  if (!pathname)
    return -EFAULT;

  /* Call VFS recursive removal */
  return vfs_rmdir_recursive(pathname);
}

int64_t sys_netinfo(void)
{
    struct net_device *dev = net_get_devices();
    if (!dev) {
        print("NET: No devices registered.\n");
        return 0;
    }

    print("--- Network Interfaces ---\n");
    while (dev) {
        print("Name: ");
        print(dev->name);
        print(" [");
        bool first = true;
        if (dev->flags & IFF_UP) { print("UP"); first = false; }
        if (dev->flags & IFF_RUNNING) { if (!first) print(", "); print("RUNNING"); first = false; }
        if (dev->flags & IFF_BROADCAST) { if (!first) print(", "); print("BROADCAST"); }
        print("] MTU: ");
        
        print_uint32(dev->mtu);
        print("\n  MAC: ");
        
        for (int i = 0; i < 6; i++) {
            print_hex8(dev->mac[i]);
            if (i < 5) print(":");
        }
        print("\n");
        dev = dev->next;
    }
    return 0;
}

int64_t sys_lsdrv(void)
{
    /* Show only registered drivers */
    ir0_driver_list_all();
    return 0;
}

int64_t sys_dmesg(void)
{
    /* Show kernel log buffer (like dmesg/journalctl) */
    extern void logging_print_buffer(void);
    logging_print_buffer();
    return 0;
}

int64_t sys_audio_test(void)
{
    if (sb16_is_available())
    {
        /* Just initialize speaker as a test or play a beep if implemented */
        sb16_speaker_on();
        print("AUDIO: SB16 Speaker toggled ON\n");
        return 0;
    }
    print("AUDIO: Sound Blaster not available\n");
    return -1;
}

int64_t sys_mouse_test(void)
{
    if (ps2_mouse_is_available())
    {
        ps2_mouse_state_t *st = ps2_mouse_get_state();
        print("MOUSE: Status: Initialized\n");
        print("MOUSE: Pos: (");
        char buf[16];
        itoa((int)st->x, buf, 10);
        print(buf);
        print(", ");
        itoa((int)st->y, buf, 10);
        print(buf);
        print(")\n");
        print("MOUSE: Buttons: L=");
        print(st->left_button ? "1" : "0");
        print(" R=");
        print(st->right_button ? "1" : "0");
        print(" M=");
        print(st->middle_button ? "1" : "0");
        print("\n");
        return 0;
    }
    print("MOUSE: PS/2 Mouse not available\n");
    return -1;
}

int64_t sys_ping(ip4_addr_t dest_ip)
{
    struct net_device *dev = net_get_devices();
    if (!dev)
    {
        print("PING: No network device available\n");
        return -1;
    }
    
    /* Use process ID as identifier, sequence 0 */
    pid_t pid = sys_getpid();
    uint16_t id = (uint16_t)(pid & 0xFFFF);
    uint16_t seq = 0;
    
    print("PING: Sending ICMP Echo Request to ");
    char ip_str[16];
    uint32_t host_ip = ntohl(dest_ip);
    itoa((host_ip >> 24) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa((host_ip >> 16) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa((host_ip >> 8) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa(host_ip & 0xFF, ip_str, 10);
    print(ip_str);
    print("\n");
    
    int ret = icmp_send_echo_request(dev, dest_ip, id, seq, NULL, 0);
    if (ret == 0)
    {
        print("PING: Echo Request sent successfully\n");
        return 0;
    }
    else
    {
        print("PING: Failed to send Echo Request\n");
        return -1;
    }
}

int64_t sys_ifconfig(ip4_addr_t ip, ip4_addr_t netmask, ip4_addr_t gateway)
{
    extern ip4_addr_t ip_local_addr;
    extern ip4_addr_t ip_netmask;
    extern ip4_addr_t ip_gateway;
    extern void arp_set_my_ip(ip4_addr_t ip);
    
    if (ip != 0)
    {
        ip_local_addr = ip;
        /* Synchronize ARP's IP address */
        arp_set_my_ip(ip);
        print("IFCONFIG: IP address set to ");
        char ip_str[16];
        uint32_t host_ip = ntohl(ip);
        itoa((host_ip >> 24) & 0xFF, ip_str, 10);
        print(ip_str);
        print(".");
        itoa((host_ip >> 16) & 0xFF, ip_str, 10);
        print(ip_str);
        print(".");
        itoa((host_ip >> 8) & 0xFF, ip_str, 10);
        print(ip_str);
        print(".");
        itoa(host_ip & 0xFF, ip_str, 10);
        print(ip_str);
        print("\n");
    }
    
    if (netmask != 0)
    {
        ip_netmask = netmask;
        print("IFCONFIG: Netmask set\n");
    }
    
    if (gateway != 0)
    {
        ip_gateway = gateway;
        print("IFCONFIG: Gateway set\n");
    }
    
    /* Show current configuration */
    print("IFCONFIG: Current configuration:\n");
    print("  IP: ");
    uint32_t host_ip = ntohl(ip_local_addr);
    char ip_str[16];
    itoa((host_ip >> 24) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa((host_ip >> 16) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa((host_ip >> 8) & 0xFF, ip_str, 10);
    print(ip_str);
    print(".");
    itoa(host_ip & 0xFF, ip_str, 10);
    print(ip_str);
    print("\n");
    
    return 0;
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
int64_t syscall_dispatch(uint64_t syscall_num, uint64_t arg1, uint64_t arg2,
                         uint64_t arg3, uint64_t arg4, uint64_t arg5)
{

  switch (syscall_num)
  {
  case 0:
    return sys_exit((int)arg1);
  case 1:
    return sys_write((int)arg1, (const void *)arg2, (size_t)arg3);
  case 2:
    return sys_read((int)arg1, (void *)arg2, (size_t)arg3);
  case 3:
    return sys_getpid();
  case 4:
    return sys_getppid();
  case 5:
    return sys_ls((const char *)arg1);
  case 6:
    return sys_mkdir((const char *)arg1, (mode_t)arg2);
  case 7:
    return sys_ps();
  case 10:
    return sys_touch((const char *)arg1);
  case 11:
    return sys_rm((const char *)arg1);
  case 12:
    return sys_fork();
  case 13:
    return sys_waitpid((pid_t)arg1, (int *)arg2, (int)arg3);
  case 40:
    return sys_rmdir((const char *)arg1);
  case 88:
    return sys_rmdir_recursive((const char *)arg1);
  case 89:
    return sys_rmdir_force((const char *)arg1);
  case 51:
    return sys_brk((void *)arg1);
  case 52:
    return (int64_t)sys_sbrk((intptr_t)arg1);
  case 53:
    return (int64_t)sys_mmap((void *)arg1, (size_t)arg2, (int)arg3, (int)arg4,
                             (int)arg5, (off_t)0);
  case 54:
    return sys_munmap((void *)arg1, (size_t)arg2);
  case 55:
    return sys_mprotect((void *)arg1, (size_t)arg2, (int)arg3);
  case 56:
    return sys_exec((const char *)arg1, (char *const *)arg2,
                    (char *const *)arg3);
  case 57:
    return sys_fstat((int)arg1, (stat_t *)arg2);
  case 58:
    return sys_stat((const char *)arg1, (stat_t *)arg2);
  case 59:
    return sys_open((const char *)arg1, (int)arg2, (mode_t)arg3);
  case 60:
    return sys_close((int)arg1);
  case 61:
    return sys_ls_detailed((const char *)arg1);
  case 19:
    return sys_lseek((int)arg1, (off_t)arg2, (int)arg3);
  case 62:
    return sys_creat((const char *)arg1, (mode_t)arg2);
  case 63:
    return sys_dup2((int)arg1, (int)arg2);
  case 79:
    return sys_getcwd((char *)arg1, (size_t)arg2);
  case 80:
    return sys_chdir((const char *)arg1);
  case 87:
    return sys_unlink((const char *)arg1);
  case 90:
    return sys_mount((const char *)arg1, (const char *)arg2,
                     (const char *)arg3);
  case 91:
    return sys_append((const char *)arg1, (const char *)arg2, (size_t)arg3);
  case 92:
    return sys_lsblk();
  case 94:
    return sys_whoami();
  case 95:
    return sys_df();
  case 100:
    return sys_chmod((const char *)arg1, (mode_t)arg2);
  case 101:
    return sys_link((const char *)arg1, (const char *)arg2);
  case 110:
    return sys_netinfo();
  case 111:
    return sys_lsdrv();
  case 112:
    return sys_audio_test();
  case 113:
    return sys_mouse_test();
  case 114:
    return sys_dmesg();
  case 115:
    return sys_ping((ip4_addr_t)arg1);
  case 116:
    return sys_ifconfig((ip4_addr_t)arg1, (ip4_addr_t)arg2, (ip4_addr_t)arg3);
  default:
    print("UNKNOWN_SYSCALL");
    print("\n");
    return -ENOSYS;
  }
}
