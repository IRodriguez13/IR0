/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs_meta.c
 * Description: stat, chmod/chown, link, rename and flock syscalls.
 */

#include "fs_syscalls_internal.h"

static int64_t do_readlinkat(int dirfd, const char *pathname, char *buf,
                             size_t bufsiz)
{
  char resolved[256];
  char kbuf[256];
  const char *target;
  size_t len;
  size_t copy_len;
  int rc;

  if (!current_process || !pathname || !buf)
    return -EFAULT;
  if (bufsiz == 0)
    return -EINVAL;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if (is_proc_path(resolved))
  {
    rc = proc_access_path(resolved, (uid_t)current_process->euid);
    if (rc != 0)
      return rc;
    rc = proc_readlink(resolved, kbuf, sizeof(kbuf));
    if (rc >= 0)
    {
      len = (size_t)rc;
      copy_len = len;
      if (copy_len > bufsiz)
        copy_len = bufsiz;
      if (copy_to_user(buf, kbuf, copy_len) != 0)
        return -EFAULT;
      return (int64_t)copy_len;
    }
    if (rc != -ENOENT)
      return rc;
  }

  rc = vfs_readlink(resolved, kbuf, sizeof(kbuf));
  if (rc >= 0)
  {
    len = (size_t)rc;
    copy_len = len;
    if (copy_len > bufsiz)
      copy_len = bufsiz;
    if (copy_to_user(buf, kbuf, copy_len) != 0)
      return -EFAULT;
    return (int64_t)copy_len;
  }
  if (rc != -ENOSYS && rc != -ENOENT && rc != -EINVAL)
    return rc;

  target = named_symlink_target(resolved);
  if (!target)
  {
    stat_t st;

    if (vfs_stat(resolved, &st) == 0)
      return -EINVAL;
    return -ENOENT;
  }

  len = strlen(target);
  copy_len = len;
  if (copy_len > bufsiz)
    copy_len = bufsiz;
  if (copy_to_user(buf, target, copy_len) != 0)
    return -EFAULT;
  return (int64_t)copy_len;
}

static int64_t do_symlinkat(const char *target, int linkdirfd,
                            const char *linkpath)
{
  char link_resolved[256];
  char target_copy[256];
  stat_t st;
  long len;
  int rc;

  if (!current_process || !target || !linkpath)
    return -EFAULT;
  len = copy_path_from_user(target_copy, target, sizeof(target_copy));
  if (len < 0)
    return (int)len;

  rc = ir0_resolve_path_at(linkdirfd, linkpath, link_resolved,
                           sizeof(link_resolved));
  if (rc != 0)
    return rc;

  if (named_symlink_stat(link_resolved, &st) == 0 ||
      named_fifo_stat(link_resolved, &st) == 0 ||
      vfs_stat(link_resolved, &st) == 0)
    return -EEXIST;

  rc = vfs_symlink(target_copy, link_resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOSYS)
    return rc;

  return named_symlink_create(link_resolved, target_copy);
}

/* fstat(2) into a kernel stat_t. */
static int64_t fs_fstat_kernel(int fd, stat_t *kst_out)
{
  stat_t kst;
  int64_t rc;

  if (!current_process)
    return -ESRCH;

  if (fd < 0)
    return -EBADF;

  if (fd < MAX_FDS_PER_PROCESS)
  {
    fd_entry_t *fdt = get_process_fd_table();

    if (fdt && fdt[fd].in_use && fdt[fd].is_pseudo && fdt[fd].vfs_file)
    {
      pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)fdt[fd].vfs_file;

      return pseudo_fs_ops_stat((const pseudo_fs_ops_t *)bind->ops,
                                bind->ctx, kst_out);
    }
  }

  if (fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_entry_t *fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_pipe)
  {
    /*
     * Linux: fstat(2) on pipe/fifo fds reports S_IFIFO. Must run before the
     * fd<=2 stdio fallback — PID1 may open /run/initctl as fd 0 when stdio
     * is not installed yet (sysvinit check_init_fifo).
     */
    rc = ir0_stat_path_routed(fd_table[fd].path, &kst);
    if (rc != 0 || !S_ISFIFO(kst.st_mode))
    {
      mode_t perm = 0600;

      if (rc == 0)
        perm = kst.st_mode & 07777;
      memset(&kst, 0, sizeof(kst));
      kst.st_mode = S_IFIFO | perm;
      kst.st_nlink = 1;
      rc = 0;
    }
  }
  else if (fd <= 2 && !stdio_is_redirected(fd_table, fd))
  {
    memset(&kst, 0, sizeof(kst));
    kst.st_dev = 0;
    kst.st_ino = (ino_t)fd;
    kst.st_mode = S_IFCHR | S_IRUSR | S_IWUSR;
    kst.st_nlink = 1;
    kst.st_uid = 0;
    kst.st_gid = 0;
    kst.st_size = 0;
    rc = 0;
  }
  else
  {
    rc = ir0_stat_path_routed(fd_table[fd].path, &kst);
    if (rc != 0)
      return rc;
  }

  *kst_out = kst;
  return 0;
}

int64_t sys_fstat(int fd, stat_t *buf)
{
  stat_t kst;
  int64_t rc;

  rc = fs_fstat_kernel(fd, &kst);
  if (rc != 0)
    return rc;
  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

int64_t sys_stat(const char *pathname, stat_t *buf)
{
  char resolved[256];
  stat_t kst;
  int64_t rc;

  if (!current_process || !pathname || !buf)
    return -EFAULT;

  memset(&kst, 0, sizeof(kst));
  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  rc = ir0_stat_path_routed_follow(resolved, &kst);

  if (rc != 0)
    return rc;
  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

/*
 * fstatat(2) into a kernel stat_t: AT_EMPTY_PATH with "" describes dirfd
 * itself (Linux vfs_fstatat()), AT_SYMLINK_NOFOLLOW the link itself.
 */
static int64_t fs_fstatat_kernel(int dirfd, const char *pathname, int flags,
                                 stat_t *kst)
{
  char resolved[256];
  int64_t rc;
  char first;

  if (!current_process)
    return -ESRCH;

  memset(kst, 0, sizeof(*kst));
  if ((flags & IR0_AT_EMPTY_PATH) &&
      copy_from_user(&first, pathname, sizeof(first)) == 0 && first == '\0')
  {
    if (dirfd != IR0_AT_FDCWD)
      return fs_fstat_kernel(dirfd, kst);
    rc = fs_resolve_at_empty(dirfd, resolved, sizeof(resolved));
  }
  else
  {
    rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  }
  if (rc != 0)
    return rc;

  if (!(flags & IR0_AT_SYMLINK_NOFOLLOW))
  {
    rc = ir0_follow_named_symlinks(resolved, sizeof(resolved),
                                   IR0_SYMLINK_FOLLOW_MAX);
    if (rc != 0)
      return rc;
  }

  return ir0_stat_path_routed(resolved, kst);
}

/*
 * sys_newfstatat - Linux newfstatat(2) subset.
 */
int64_t sys_newfstatat(int dirfd, const char *pathname, stat_t *buf, int flags)
{
  stat_t kst;
  int64_t rc;

  rc = fs_fstatat_kernel(dirfd, pathname, flags, &kst);
  if (rc != 0)
    return rc;
  if (ir0_copy_stat_to_user(buf, &kst) != 0)
    return -EFAULT;
  return 0;
}

/* Linux struct statx (include/uapi/linux/stat.h), 256 bytes. */
struct statx_timestamp_user
{
  int64_t tv_sec;
  uint32_t tv_nsec;
  int32_t reserved;
};

struct statx_user
{
  uint32_t stx_mask;
  uint32_t stx_blksize;
  uint64_t stx_attributes;
  uint32_t stx_nlink;
  uint32_t stx_uid;
  uint32_t stx_gid;
  uint16_t stx_mode;
  uint16_t spare0;
  uint64_t stx_ino;
  uint64_t stx_size;
  uint64_t stx_blocks;
  uint64_t stx_attributes_mask;
  struct statx_timestamp_user stx_atime;
  struct statx_timestamp_user stx_btime;
  struct statx_timestamp_user stx_ctime;
  struct statx_timestamp_user stx_mtime;
  uint32_t stx_rdev_major;
  uint32_t stx_rdev_minor;
  uint32_t stx_dev_major;
  uint32_t stx_dev_minor;
  uint64_t spare2[14];
};

_Static_assert(sizeof(struct statx_user) == 256, "struct statx is 256 bytes");

#define STATX_BASIC_STATS 0x7ffU
#define STATX_RESERVED 0x80000000U
#define STATX_AT_NO_AUTOMOUNT 0x800
#define STATX_AT_SYNC_TYPE 0x6000

/* Linux new_decode_dev(): the major/minor halves of a dev_t. */
static uint32_t statx_dev_major(uint64_t dev)
{
  return (uint32_t)((dev >> 8) & 0xfffU);
}

static uint32_t statx_dev_minor(uint64_t dev)
{
  return (uint32_t)((dev & 0xffU) | ((dev >> 12) & 0xfff00U));
}

/*
 * sys_statx - statx(2): the fstatat() lookup, reported in struct statx.
 * Every basic field is filled whatever @mask asks (Linux may return more
 * than requested); birth time is not available.
 */
int64_t sys_statx(int dirfd, const char *pathname, int flags,
                  unsigned int mask, void *buf)
{
  struct statx_user sx;
  stat_t kst;
  int64_t rc;

  if (flags & ~(IR0_AT_SYMLINK_NOFOLLOW | STATX_AT_NO_AUTOMOUNT |
                IR0_AT_EMPTY_PATH | STATX_AT_SYNC_TYPE))
    return -EINVAL;
  if ((flags & STATX_AT_SYNC_TYPE) == STATX_AT_SYNC_TYPE)
    return -EINVAL;
  if (mask & STATX_RESERVED)
    return -EINVAL;

  rc = fs_fstatat_kernel(dirfd, pathname, flags, &kst);
  if (rc != 0)
    return rc;

  memset(&sx, 0, sizeof(sx));
  sx.stx_mask = STATX_BASIC_STATS;
  sx.stx_blksize = (uint32_t)kst.st_blksize;
  sx.stx_nlink = (uint32_t)kst.st_nlink;
  sx.stx_uid = (uint32_t)kst.st_uid;
  sx.stx_gid = (uint32_t)kst.st_gid;
  sx.stx_mode = (uint16_t)kst.st_mode;
  sx.stx_ino = (uint64_t)kst.st_ino;
  sx.stx_size = (uint64_t)kst.st_size;
  sx.stx_blocks = (uint64_t)kst.st_blocks;
  sx.stx_atime.tv_sec = (int64_t)kst.st_atime;
  sx.stx_ctime.tv_sec = (int64_t)kst.st_ctime;
  sx.stx_mtime.tv_sec = (int64_t)kst.st_mtime;
  sx.stx_rdev_major = statx_dev_major((uint64_t)kst.st_rdev);
  sx.stx_rdev_minor = statx_dev_minor((uint64_t)kst.st_rdev);
  sx.stx_dev_major = statx_dev_major((uint64_t)kst.st_dev);
  sx.stx_dev_minor = statx_dev_minor((uint64_t)kst.st_dev);
  if (copy_to_user(buf, &sx, sizeof(sx)) != 0)
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

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  /* rmdir(2): only an empty directory goes; never the tree under it. */
  if (flags & AT_REMOVEDIR)
    return vfs_rmdir(resolved);

  rc = named_symlink_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  rc = named_fifo_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  rc = named_socket_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  rc = named_devnode_unlink(resolved);
  if (rc == 0)
    return 0;
  if (rc != -ENOENT)
    return rc;

  if (posix_shm_path_is(resolved))
    return posix_shm_try_unlink(resolved);

  /* unlink(2) of a directory is EISDIR; only rmdir removes one. */
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

  rc = ir0_resolve_path_at(olddirfd, oldpath, old_resolved, sizeof(old_resolved));
  if (rc != 0)
    return rc;
  rc = ir0_resolve_path_at(newdirfd, newpath, new_resolved, sizeof(new_resolved));
  if (rc != 0)
    return rc;

  rc = vfs_rename(old_resolved, new_resolved);
  if (rc != 0)
    return rc;

  /* The target name now belongs to the renamed file. */
  (void)named_fifo_unlink(new_resolved);
  (void)named_symlink_unlink(new_resolved);
  return 0;
}

int64_t sys_readlink(const char *pathname, char *buf, size_t bufsiz)
{
  return do_readlinkat(IR0_AT_FDCWD, pathname, buf, bufsiz);
}

int64_t sys_readlinkat(int dirfd, const char *pathname, char *buf, size_t bufsiz)
{
  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;
  return do_readlinkat(dirfd, pathname, buf, bufsiz);
}

int64_t sys_symlink(const char *target, const char *linkpath)
{
  return do_symlinkat(target, IR0_AT_FDCWD, linkpath);
}

int64_t sys_symlinkat(const char *target, int dirfd, const char *linkpath)
{
  if (dirfd != IR0_AT_FDCWD)
    return -ENOSYS;
  return do_symlinkat(target, dirfd, linkpath);
}

static int fs_resolve_fchmodat_path(int dirfd, const char *pathname, char *resolved,
                                  size_t resolved_sz, int flags)
{
  char path_copy[256];
  long len;
  int rc;

  if (!pathname)
    return -EFAULT;
  if (flags & ~(IR0_AT_SYMLINK_NOFOLLOW | IR0_AT_EMPTY_PATH))
    return -EINVAL;
  len = copy_path_from_user(path_copy, pathname, sizeof(path_copy));
  if (len < 0)
    return (int)len;

  if (path_copy[0] == '\0')
  {
    if (!(flags & IR0_AT_EMPTY_PATH))
      return -ENOENT;
    return fs_resolve_at_empty(dirfd, resolved, resolved_sz);
  }

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, resolved_sz);
  return rc;
}

int64_t sys_fchmod(int fd, mode_t mode)
{
  char resolved[256];
  stat_t st;
  fd_entry_t *fd_table;
  int rc;

  if (!current_process)
    return -ESRCH;
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use || fd_table[fd].path[0] == '\0')
    return -EBADF;

  rc = fs_resolve_at_empty(fd, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  rc = vfs_stat(resolved, &st);
  if (rc != 0)
    return rc;

  if (current_process->euid != ROOT_UID && current_process->euid != st.st_uid)
    return -EPERM;

  return vfs_chmod(resolved, mode);
}

int64_t sys_fchown(int fd, uid_t owner, gid_t group)
{
  char resolved[256];
  fd_entry_t *fd_table;
  int rc;

  if (!current_process)
    return -ESRCH;
  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use || fd_table[fd].path[0] == '\0')
    return -EBADF;

  rc = fs_resolve_at_empty(fd, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if (current_process->euid != ROOT_UID)
    return -EPERM;

  return ir0_chown_path_routed(resolved, owner, group);
}

int64_t sys_fchmodat(int dirfd, const char *pathname, mode_t mode, int flags)
{
  char resolved[256];
  stat_t st;
  int rc;

  if (!current_process)
    return -ESRCH;

  rc = fs_resolve_fchmodat_path(dirfd, pathname, resolved, sizeof(resolved), flags);
  if (rc != 0)
    return rc;

  rc = vfs_stat(resolved, &st);
  if (rc != 0)
    return rc;

  if (current_process->euid != ROOT_UID && current_process->euid != st.st_uid)
    return -EPERM;

  return vfs_chmod(resolved, mode);
}

int64_t sys_fchownat(int dirfd, const char *pathname, uid_t owner, gid_t group,
		     int flags)
{
  char resolved[256];
  int rc;

  if (!current_process)
    return -ESRCH;

  rc = fs_resolve_fchmodat_path(dirfd, pathname, resolved, sizeof(resolved), flags);
  if (rc != 0)
    return rc;

  if (current_process->euid != ROOT_UID)
    return -EPERM;

  return ir0_chown_path_routed(resolved, owner, group);
}

int64_t sys_flock(int fd, int operation)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use)
    return -EBADF;

  /* Pipes and pseudo fds have no open file description to lock. */
  if (fd_table[fd].is_pipe || !fd_table[fd].vfs_file)
    return -EINVAL;

  return ir0_flock_apply((struct vfs_file *)fd_table[fd].vfs_file, operation);
}

int64_t sys_fchdir(int fd)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[fd].in_use)
    return -EBADF;

  if (fd_table[fd].is_pipe || fd_table[fd].is_socket)
    return -EBADF;

  if (fd_table[fd].path[0] == '\0')
    return -EBADF;

  /*
   * fd_table[].path is a kernel string. sys_chdir() rejects non-userspace
   * pointers with -EFAULT, which broke runsvdir's fchdir(curdir) loop and
   * left services with a wrong cwd (./run ENOENT, supervise create fails).
   */
  return ir0_chdir_resolved(fd_table[fd].path);
}
