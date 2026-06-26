/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fs_path_syscalls.c
 * Description: path-based filesystem syscalls (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../syscalls.h"
#include "../process.h"
#include "fs_path_syscalls.h"
#include "syscalls_glue.h"
#include <config.h>
#include <ir0/chmod.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/fase52_debug.h>
#include <ir0/named_fifo.h>
#include <ir0/named_symlink.h>
#include <ir0/path.h>
#include <ir0/path_routed.h>
#include <ir0/path_user.h>
#include <ir0/permissions.h>
#include <ir0/stat.h>
#include <ir0/utimens.h>
#include <ir0/utsname.h>
#include <ir0/version.h>
#include <ir0/vfs.h>
#include <stddef.h>
#include <string.h>

static void process_cwd_ensure_absolute(process_t *proc)
{
  if (!proc)
    return;
  if (proc->cwd[0] == '/')
    return;
  strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
  proc->cwd[sizeof(proc->cwd) - 1] = '\0';
}

int64_t sys_mount(const char *dev, const char *mountpoint, const char *fstype)
{
  char mountpoint_resolved[256];
  char dev_copy[256];
  char dev_resolved[256];
  char fstype_buf[32];
  const char *mount_path;
  const char *dev_path;
  const char *mount_fstype;
  int dev_is_pseudo = 0;
  int rc;

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

  rc = ir0_resolve_user_path(mountpoint, mountpoint_resolved,
                             sizeof(mountpoint_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;
  mount_path = mountpoint_resolved;

  if (copy_from_user_cstring(dev_copy, sizeof(dev_copy), dev) != 0)
    return -EFAULT;

  if (fstype)
  {
    if (copy_from_user_cstring(fstype_buf, sizeof(fstype_buf), fstype) != 0)
      return -EFAULT;
    mount_fstype = fstype_buf[0] ? fstype_buf : CONFIG_ROOT_FILESYSTEM;
  }
  else
  {
    mount_fstype = CONFIG_ROOT_FILESYSTEM;
  }

  /* tmpfs accepts pseudo device strings for Unix-like parity (e.g. none). */
  if (strcmp(mount_fstype, "tmpfs") == 0 &&
      (strcmp(dev_copy, "none") == 0 || strcmp(dev_copy, "tmpfs") == 0))
  {
    dev_is_pseudo = 1;
    dev_path = dev_copy;
  }
  else
  {
    rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, NULL, dev_copy, dev_resolved,
                              sizeof(dev_resolved), current_process->cwd);
    if (rc != 0)
      return rc;
    dev_path = dev_resolved;
  }

  /* Validate device path unless pseudo device was allowed. */
  if (!dev_is_pseudo && (dev_path[0] != '/' || strlen(dev_path) >= 256))
  {
    sys_write(STDERR_FILENO, "mount: invalid device path\n", 27);
    return -EINVAL;
  }

  /* Validate mountpoint path */
  if (mount_path[0] != '/' || strlen(mount_path) >= 256)
  {
    sys_write(STDERR_FILENO, "mount: invalid mount point\n", 27);
    return -EINVAL;
  }

  /* Check if mountpoint exists and is a directory */
  stat_t st;
  if (vfs_stat(mount_path, &st) < 0)
  {
    sys_write(STDERR_FILENO, "mount: mount point does not exist\n", 34);
    return -ENOENT;
  }
  if (!S_ISDIR(st.st_mode))
  {
    sys_write(STDERR_FILENO, "mount: mount point is not a directory\n", 38);
    return -ENOTDIR;
  }
  rc = vfs_mount(dev_path, mount_path, mount_fstype);
  if (rc < 0)
  {
    /* Report specific error (skip noise for expected EBUSY remount attempts). */
    if (rc != -EBUSY)
    {
      sys_write(STDERR_FILENO, "mount: failed to mount ", 22);
      sys_write(STDERR_FILENO, mount_fstype, strlen(mount_fstype));
      sys_write(STDERR_FILENO, " filesystem\n", 12);
    }
    return rc;
  }

  return rc;
}

int64_t sys_umount(const char *target, int flags)
{
  char target_resolved[256];
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!target)
    return -EFAULT;

  if (validate_userspace_string(target, 256) != 0)
    return -EFAULT;

  if (flags != 0)
    return -EINVAL;

  rc = ir0_resolve_user_path(target, target_resolved, sizeof(target_resolved),
                             current_process->cwd);
  if (rc != 0)
    return rc;

  if (target_resolved[0] != '/' || strlen(target_resolved) >= 256)
  {
    sys_write(STDERR_FILENO, "umount: invalid target path\n", 28);
    return -EINVAL;
  }

  stat_t st;
  if (vfs_stat(target_resolved, &st) < 0)
  {
    sys_write(STDERR_FILENO, "umount: target path does not exist\n", 35);
    return -ENOENT;
  }

  return vfs_umount(target_resolved);
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

  (void)named_fifo_unlink(new_resolved);
  (void)named_symlink_unlink(new_resolved);

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
                              (gid_t)current_process->egid,
                              current_process->groups,
                              current_process->ngroups);
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

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  /*
   * IR0 currently evaluates access permissions against effective IDs.
   * AT_EACCESS is accepted and maps to this same behavior.
   * AT_SYMLINK_NOFOLLOW is accepted as a no-op because symlinks are not implemented.
   */
  return ir0_access_path_routed(resolved, mode,
                                (uid_t)current_process->euid,
                                (gid_t)current_process->egid,
                                current_process->groups,
                                current_process->ngroups);
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

  if (is_absolute_path(pathname))
  {
    /* Absolute path - just normalize it */
    if (normalize_path(pathname, new_path, sizeof(new_path)) != 0)
      return -ENAMETOOLONG;
  }
  else
  {
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
                               (gid_t)current_process->egid,
                               current_process->groups,
                               current_process->ngroups);
  if (ret != 0)
    return ret;

  /* Update current working directory */
  strncpy(current_process->cwd, new_path, sizeof(current_process->cwd) - 1);
  current_process->cwd[sizeof(current_process->cwd) - 1] = '\0';
  process_cwd_ensure_absolute(current_process);

  return 0;
}

int64_t sys_getcwd(char *buf, size_t size)
{
  size_t len;

  if (!current_process || !buf || size == 0)
    return -EFAULT;

  if (validate_userspace_buffer(buf, size) != 0)
    return -EFAULT;

  process_cwd_ensure_absolute(current_process);

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

int64_t sys_ftruncate(int fd, off_t length)
{
  fd_entry_t *fd_table;
  const char *path;
  stat_t st;

  if (!current_process)
    return -ESRCH;
  if (length < 0)
    return -EINVAL;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  /* Pseudo /proc / /sys fds are not path-backed regular files. */
  if (fd >= FD_PROC_BASE && fd < FD_SYS_BASE + FD_RANGE_SIZE)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_pipe || fd_table[fd].is_socket)
    return -EBADF;

  if (fd <= 2 && !fd_table[fd].vfs_file && !fd_table[fd].is_devfs)
    return -EBADF;

  path = fd_table[fd].path;
  if (!path || path[0] != '/')
    return -EBADF;

  if (vfs_stat(path, &st) != 0)
    return -EBADF;

  if (S_ISDIR(st.st_mode))
    return -EINVAL;

  return vfs_truncate(path, (size_t)length);
}
