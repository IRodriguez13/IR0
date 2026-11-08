/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs_syscalls.c
 * Description: open/openat/mknod and path-resolution syscalls.
 */

#include "fs_syscalls_internal.h"


static int mknod_prepare_fifo_path(char *resolved, size_t resolved_sz)
{
  stat_t st;
  stat_t vst;
  int rc;
  int have_named;

  rc = ir0_follow_named_symlinks(resolved, resolved_sz, IR0_SYMLINK_FOLLOW_MAX);
  if (rc != 0)
    return rc;

  {
    char norm[256];

    if (normalize_path(resolved, norm, sizeof(norm)) != 0)
      return -ENAMETOOLONG;
    strncpy(resolved, norm, resolved_sz - 1);
    resolved[resolved_sz - 1] = '\0';
  }

  have_named = (named_fifo_stat(resolved, &st) == 0);
  if (have_named && vfs_stat(resolved, &vst) != 0)
    return 0;

  rc = vfs_clear_stale_for_regular_file(resolved);
  if (rc != 0)
    return rc;

  if (vfs_stat(resolved, &st) == 0)
  {
    if (S_ISDIR(st.st_mode))
      rc = vfs_rmdir_recursive(resolved);
    else
      rc = vfs_unlink(resolved);
    if (rc != 0)
      return rc;
  }

  return 0;
}

static void mknod_purge_vfs_shadow(const char *path)
{
  stat_t st;

  while (vfs_stat(path, &st) == 0)
  {
    if (S_ISDIR(st.st_mode))
    {
      if (vfs_rmdir_recursive(path) != 0)
        break;
    }
    else if (vfs_unlink(path) != 0)
      break;
  }
}

static int64_t mknod_create_named_fifo(const char *resolved, mode_t mode)
{
  int rc;

  if (!current_process)
    return -ESRCH;
  rc = named_fifo_create_owned(resolved, mode,
                               (uid_t)current_process->euid,
                               (gid_t)current_process->egid);
  if (rc != 0)
    return rc;

  mknod_purge_vfs_shadow(resolved);
  return 0;
}

static int fs_dirfd_base_path(int dirfd, char *base, size_t base_sz)
{
  fd_entry_t *fdt;
  stat_t st;
  const char *cwd;

  if (!current_process)
    return -ESRCH;

  if (dirfd == IR0_AT_FDCWD)
  {
    cwd = current_process->cwd;
    if (!cwd || cwd[0] != '/')
      cwd = "/";
    strncpy(base, cwd, base_sz - 1);
    base[base_sz - 1] = '\0';
    return 0;
  }

  fdt = get_process_fd_table();
  if (!fdt || dirfd < 0 || dirfd >= MAX_FDS_PER_PROCESS || !fdt[dirfd].in_use)
    return -EBADF;
  if (fdt[dirfd].path[0] != '/')
    return -EBADF;
  if (ir0_stat_path_routed(fdt[dirfd].path, &st) != 0)
    return -ENOTDIR;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  strncpy(base, fdt[dirfd].path, base_sz - 1);
  base[base_sz - 1] = '\0';
  return 0;
}

int ir0_resolve_path_at(int dirfd, const char *user_path, char *resolved,
                        size_t resolved_sz)
{
  char path_copy[256];
  char dirpath[256];
  char joined[256];
  long len;
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!user_path || !resolved || resolved_sz == 0)
    return -EFAULT;
  len = strncpy_from_user(path_copy, user_path, sizeof(path_copy));
  if (len < 0 || (size_t)len == sizeof(path_copy))
    return -EFAULT;

  if (path_copy[0] == '/')
  {
    return ir0_resolve_kpath_at(IR0_AT_FDCWD, path_copy, resolved, resolved_sz,
                                current_process->cwd, current_process->root);
  }

  if (dirfd != IR0_AT_FDCWD)
  {
    rc = fs_dirfd_base_path(dirfd, dirpath, sizeof(dirpath));
    if (rc != 0)
      return rc;
    if (join_paths(dirpath, path_copy, joined, sizeof(joined)) != 0)
      return -ENAMETOOLONG;
    /*
     * dirfd path is already host-absolute (may be outside the jail).
     * Do not re-apply process root (Linux openat via outside fd).
     */
    if (normalize_path(joined, resolved, resolved_sz) != 0)
      return -ENAMETOOLONG;
    return 0;
  }

  return ir0_resolve_user_path_at(dirfd, user_path, resolved, resolved_sz,
                                  current_process->cwd, current_process->root);
}

static int named_fifo_open_wait_peer(pipe_t *pipe, int end, int ir0_flags)
{
  int nonblock = (ir0_flags & IR0_O_NONBLOCK) != 0;

  if (!pipe || !pipe->named)
    return 0;

  for (;;)
  {
    if (pipe_named_peer_is_open(pipe, end))
      return 0;

    if (nonblock)
    {
      /*
       * Linux fifo(7): O_WRONLY|O_NONBLOCK with no reader → ENXIO.
       * O_RDONLY|O_NONBLOCK opens immediately (EOF on read until writer).
       */
      if (end == 1)
        return -ENXIO;
      return 0;
    }

    if (signals_pause_should_interrupt(current_process))
      return -EINTR;

    {
      int64_t sleep_ret = syscall_sleep_ms_locked(20);

      if (sleep_ret < 0)
        return (int)sleep_ret;
    }
  }
}

static int open_named_fifo_fd(const char *path, int ir0_flags)
{
  pipe_t *pipe;
  fd_entry_t *fd_table;
  int accmode;
  int end;
  int fd = -1;
  int wait_rc;

  pipe = named_fifo_lookup(path);
  if (!pipe)
    return -ENOENT;

  accmode = ir0_flags & O_ACCMODE;
  if (accmode == O_WRONLY)
    end = 1;
  else if (accmode == O_RDONLY || accmode == O_RDWR)
    end = 0;
  else
    return -EINVAL;

  fd_table = get_process_fd_table();
  if (!fd_table)
    return -ESRCH;

  fd = fd_alloc_lowest(fd_table, 0);
  if (fd < 0)
    return -EMFILE;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].offset = 0;
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = (void *)pipe;
  fd_table[fd].is_pipe = true;
  fd_table[fd].pipe_end = end;
  fd_table[fd].is_devfs = false;
  fd_table[fd].is_socket = false;

  pipe_fd_entry_acquire_refs(&fd_table[fd]);
  wait_rc = named_fifo_open_wait_peer(pipe, end, ir0_flags);
  if (wait_rc != 0)
  {
    pipe_fd_entry_release_refs(pipe, &fd_table[fd]);
    fd_table[fd].in_use = false;
    fd_table[fd].vfs_file = NULL;
    fd_table[fd].is_pipe = false;
    return wait_rc;
  }

  fd_slot_note_created(&fd_table[fd]);
  return fd;
}

static int devfs_bind_fd_slot(const char *path, devfs_node_t *node, int ir0_flags)
{
  fd_entry_t *fd_table = get_process_fd_table();
  int fd = -1;
  int i;

  if (!fd_table || !node || !path)
    return -EINVAL;

  for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }
  if (fd < 0)
    return -EMFILE;

  memset(&fd_table[fd], 0, sizeof(fd_table[fd]));
  fd_table[fd].in_use = true;
  fd_entry_devfs_bind(&fd_table[fd], node);
  fd_table[fd].is_pipe = false;
  fd_table[fd].pipe_end = -1;
  fd_table[fd].vfs_file = NULL;
  fd_table[fd].offset = 0;
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = 0;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';

  if (devfs_is_ptmx_device(node->entry.device_id))
  {
    int slot = devfs_pty_take_pending_master_slot();

    if (slot < 0)
    {
      fd_table[fd].in_use = false;
      return -EIO;
    }
    fd_table[fd].vfs_file = devfs_pty_vfs_mark(slot);
  }

  /*
   * Finite text devices: capture a per-open snapshot so read() honors
   * offset and reaches EOF (BusyBox cat). Failure falls back to ops->read
   * bounce+slice.
   */
  if (devfs_node_wants_text_snap(node->entry.device_id))
    fd_table[fd].vfs_file = devfs_text_snap_capture(node->entry.device_id);

  if (DEBUG_VFS)
  {
    klog_debug_fmt("VFS", "[VFS][OPEN] path=%s fd=%llx dev_id=%x", path, (unsigned long long)((uint64_t)fd), (unsigned)(node->entry.device_id));
  }

  /*
   * devfs is a resource provider just like VFS, pipes and sockets.  Register
   * its lifecycle before the descriptor becomes observable by fork/dup/exit.
   */
  fd_slot_note_created(&fd_table[fd]);
  return fd;
}

static int64_t sys_open_vfs_resolved(char *path_to_use, int ir0_flags,
                                     int linux_open_flags, mode_t mode,
                                     int dirfd_dbg)
{
  int flags;
  int64_t open_ret;
  int path_rc;
  fd_entry_t *fd_table;
  int fd;
  struct vfs_file *vfs_file;
  int ret;

  (void)linux_open_flags;
  (void)dirfd_dbg;

  if (!path_to_use || path_to_use[0] != '/')
    return -EINVAL;

  flags = ir0_flags;

  if (!(ir0_flags & O_DIRECTORY))
  {
    path_rc = ir0_follow_named_symlinks(path_to_use, 256,
                                        IR0_SYMLINK_FOLLOW_MAX);
    if (path_rc != 0)
      return path_rc;
  }

  {
    int prep_rc = ir0_supervise_prepare_open(path_to_use, ir0_flags);

    if (prep_rc != 0)
      return prep_rc;
  }

  if (named_fifo_path_must_be_fifo(path_to_use))
  {
    stat_t vst;
    char prep[256];

    if (vfs_stat(path_to_use, &vst) == 0 && S_ISREG(vst.st_mode))
      vfs_clear_stale_for_regular_file(path_to_use);
    if (!named_fifo_lookup(path_to_use))
    {
      strncpy(prep, path_to_use, sizeof(prep) - 1);
      prep[sizeof(prep) - 1] = '\0';
      if (mknod_prepare_fifo_path(prep, sizeof(prep)) == 0 &&
          named_fifo_create_owned(prep, 0600,
                                  (uid_t)current_process->euid,
                                  (gid_t)current_process->egid) == 0)
        mknod_purge_vfs_shadow(prep);
    }
  }

  if (named_fifo_lookup(path_to_use))
  {
    path_rc = ir0_open_access_path_routed(path_to_use, ir0_flags,
                                          (uid_t)current_process->euid,
                                          (gid_t)current_process->egid);
    if (path_rc != 0)
      return path_rc;
    open_ret = open_named_fifo_fd(path_to_use, ir0_flags);
    return open_ret;
  }

  if (posix_shm_path_is(path_to_use))
  {
    open_ret = posix_shm_try_open(path_to_use, ir0_flags, 0600);
    return open_ret;
  }

  /*
   * A device node created by mknod outside /dev: hand the open to the devfs
   * node owning that (major,minor), so the fd behaves exactly like the one
   * from /dev — same ops, same refcount, no second I/O path to maintain.
   */
  {
    mode_t dn_mode = 0;
    dev_t dn_rdev = 0;

    if (named_devnode_lookup(path_to_use, &dn_mode, &dn_rdev) == 0)
    {
      devfs_node_t *dn = devfs_find_node_by_rdev((uint32_t)dn_rdev);

      int64_t drc;

      if (!dn)
        return -ENXIO;
      drc = ir0_open_access_path_routed(path_to_use, ir0_flags,
                                        (uid_t)current_process->euid,
                                        (gid_t)current_process->egid);
      if (drc != 0)
        return drc;
      ensure_devfs_init();
      drc = devfs_open_node(dn, ir0_flags);
      if (drc < 0)
        return drc;
      open_ret = devfs_bind_fd_slot(path_to_use, dn, ir0_flags);
      if (open_ret < 0)
      {
        devfs_pty_abort_pending_master();
        devfs_close_node(dn);
      }
      return open_ret;
    }
  }

  if (flags & O_DIRECTORY)
  {
    if (!check_file_access(path_to_use, ACCESS_EXEC, current_process))
    {
      return -EACCES;
    }
  }
  else if (flags & O_CREAT)
  {
    stat_t st;

    if (vfs_stat(path_to_use, &st) != 0)
    {
      char parent[256];
      stat_t pst;

      if (get_parent_path(path_to_use, parent, sizeof(parent)) != 0)
        return -ENAMETOOLONG;
      /*
       * Missing parent is ENOENT (Linux). check_file_access() returns false
       * when stat fails — do not mask that as EACCES (runsv pid.new noise).
       */
      if (vfs_stat(parent, &pst) != 0)
      {
        return -ENOENT;
      }
      if (!check_file_access(parent, ACCESS_EXEC | ACCESS_WRITE, current_process))
      {
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
        return -EACCES;
      }
    }
  }
  else
  {
    int access_mode = 0;
    int accmode = flags & O_ACCMODE;
    stat_t st;

    if (accmode == O_RDONLY || accmode == O_RDWR)
      access_mode |= ACCESS_READ;
    if (accmode == O_WRONLY || accmode == O_RDWR)
      access_mode |= ACCESS_WRITE;
    /*
     * Linux: missing path → ENOENT from lookup, not EACCES from a prior
     * permission probe on a non-existent inode.
     */
    if (access_mode && vfs_stat(path_to_use, &st) == 0 &&
        !check_file_access(path_to_use, access_mode, current_process))
    {
      return -EACCES;
    }
  }

  fd_table = get_process_fd_table();
  fd = -1;
  fd = fd_alloc_lowest(fd_table, 0);

  if (fd == -1)
  {
    return -EMFILE;
  }

  vfs_file = NULL;
  ret = vfs_open(path_to_use, flags, mode, &vfs_file);
  if (ret != 0)
  {
    return ret;
  }

  if (flags & O_DIRECTORY)
  {
    stat_t st;

    if (ir0_stat_path_routed(path_to_use, &st) < 0)
    {
      if (vfs_file)
        vfs_close(vfs_file);
      return -ENOTDIR;
    }
    if (!S_ISDIR(st.st_mode))
    {
      if (vfs_file)
        vfs_close(vfs_file);
      return -ENOTDIR;
    }
  }

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path_to_use, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = flags;
  fd_table[fd].fd_flags = (flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = vfs_file;
  if (flags & O_DIRECTORY)
    fd_table[fd].offset = 0;
  else
    fd_table[fd].offset = vfs_file ? vfs_file->pos : 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_entry_devfs_unbind(&fd_table[fd]);
  fd_table[fd].is_pseudo = false;
  fd_table[fd].is_epoll = false;
  fd_table[fd].pipe_end = -1;
  fd_slot_note_created(&fd_table[fd]);

  return fd;
}

static int64_t pseudo_bind_dir_fd(const char *path, int ir0_flags)
{
  fd_entry_t *fd_table;
  stat_t st;
  int access_rc;
  int fd;

  if (!path || !current_process)
    return -EINVAL;
  if (!(ir0_flags & O_DIRECTORY))
    return -EISDIR;

  if (ir0_stat_path_routed(path, &st) != 0)
    return -ENOENT;
  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;
  access_rc = ir0_open_access_path_routed(path, ir0_flags,
                                          (uid_t)current_process->euid,
                                          (gid_t)current_process->egid);
  if (access_rc != 0)
    return access_rc;
  if (!ir0_access_from_stat(&st, ACCESS_EXEC,
                            (uid_t)current_process->euid,
                            (gid_t)current_process->egid))
    return -EACCES;

  fd_table = get_process_fd_table();
  fd = -1;
  for (int i = 0; i < MAX_FDS_PER_PROCESS; i++)
  {
    if (!fd_table[i].in_use)
    {
      fd = i;
      break;
    }
  }
  if (fd < 0)
    return -EMFILE;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = NULL;
  fd_table[fd].offset = 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_entry_devfs_unbind(&fd_table[fd]);
  fd_table[fd].is_pseudo = false;
  fd_table[fd].pipe_end = -1;
  fd_slot_note_created(&fd_table[fd]);
  return fd;
}

/*
 * Install a registry /proc|/sys|/heart file into the process fd_table.
 * Returns a real slot index (not PSEUDO_FS_*_FD_BASE).
 */
static int64_t pseudo_bind_file_fd(const char *path, int ir0_flags)
{
  fd_entry_t *fd_table;
  const pseudo_fs_ops_t *ops = NULL;
  void *ctx = NULL;
  pseudo_fd_bind_t *bind;
  int dynamic = 0;
  int fd;
  int64_t rc;

  if (!path || !current_process)
    return -EINVAL;

  rc = ir0_open_access_path_routed(path, ir0_flags,
                                   (uid_t)current_process->euid,
                                   (gid_t)current_process->egid);
  if (rc != 0)
    return rc;

  pseudo_fs_nodes_register_all();
  rc = pseudo_fs_acquire_path(path, ir0_flags, &ops, &ctx, &dynamic);
  if (rc != 0)
    return rc;

  fd_table = get_process_fd_table();
  fd = -1;
  fd = fd_alloc_lowest(fd_table, 0);
  if (fd < 0)
  {
    (void)pseudo_fs_release_ops(ops, ctx, dynamic);
    return -EMFILE;
  }

  bind = (pseudo_fd_bind_t *)kmalloc(sizeof(*bind));
  if (!bind)
  {
    (void)pseudo_fs_release_ops(ops, ctx, dynamic);
    return -ENOMEM;
  }
  bind->ops = ops;
  bind->ctx = ctx;
  bind->refs = 1;
  bind->dynamic = dynamic;

  fd_table[fd].in_use = true;
  strncpy(fd_table[fd].path, path, sizeof(fd_table[fd].path) - 1);
  fd_table[fd].path[sizeof(fd_table[fd].path) - 1] = '\0';
  fd_table[fd].flags = ir0_flags;
  fd_table[fd].fd_flags = (ir0_flags & O_CLOEXEC) ? FD_CLOEXEC : 0;
  fd_table[fd].vfs_file = bind;
  fd_table[fd].offset = 0;
  fd_table[fd].is_pipe = false;
  fd_table[fd].is_socket = false;
  fd_entry_devfs_unbind(&fd_table[fd]);
  fd_table[fd].is_pseudo = true;
  fd_table[fd].pipe_end = -1;
  fd_slot_note_created(&fd_table[fd]);
  return fd;
}

static int64_t devfs_open_resolved_node(const char *path, int ir0_flags)
{
  devfs_node_t *node;
  int64_t drc;
  int64_t open_ret;

  ensure_devfs_init();
  node = devfs_find_node(path);
  if (!node)
  {
    return -ENOENT;
  }
  drc = ir0_open_access_path_routed(path, ir0_flags,
                                    (uid_t)current_process->euid,
                                    (gid_t)current_process->egid);
  if (drc != 0)
  {
    return drc;
  }
  drc = devfs_open_node(node, ir0_flags);
  if (drc < 0)
  {
    return drc;
  }
  open_ret = devfs_bind_fd_slot(path, node, ir0_flags);
  if (open_ret < 0)
  {
    devfs_pty_abort_pending_master();
    devfs_close_node(node);
    return open_ret;
  }
  return open_ret;
}

/*
 * Route open(2) after path resolution so cwd-relative paths like ./stdin
 * from /dev reach devfs (same order as openat/stat).
 */
static int64_t sys_open_routed_resolved(char *resolved, int ir0_flags,
                                        int linux_open_flags, mode_t mode,
                                        int dirfd_dbg)
{
  int64_t open_ret;

  if (is_proc_path(resolved))
  {
    if (strcmp(resolved, "/proc") == 0 || strcmp(resolved, "/proc/") == 0 ||
        proc_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    return open_ret;
  }

  if (is_sys_path(resolved))
  {
    if (strcmp(resolved, "/sys") == 0 || strcmp(resolved, "/sys/") == 0 ||
        sysfs_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    return open_ret;
  }

  if (is_heart_path(resolved) && !heart_is_dennis_vfs_path(resolved))
  {
    char canon[256];

    heart_nodes_register();
    if (heart_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    if (heart_alias_canonical(resolved, canon, sizeof(canon)))
    {
      open_ret = pseudo_bind_file_fd(canon, ir0_flags);
      return open_ret;
    }
    open_ret = pseudo_bind_file_fd(resolved, ir0_flags);
    return open_ret;
  }

  if (ir0_is_dev_path(resolved))
  {
    if (posix_shm_path_is(resolved))
    {
      open_ret = posix_shm_try_open(resolved, ir0_flags, mode);
      return open_ret;
    }
    if (strcmp(resolved, "/dev") == 0 || strcmp(resolved, "/dev/") == 0 ||
        devfs_is_virtual_subdir(resolved))
    {
      if (!(ir0_flags & O_DIRECTORY))
        return -EISDIR;
      ensure_devfs_init();
      return pseudo_bind_dir_fd(resolved, ir0_flags);
    }
    if (strncmp(resolved, "/dev/", 5) == 0)
    {
      ensure_devfs_init();
      return devfs_open_resolved_node(resolved, ir0_flags);
    }
    return -ENOENT;
  }

  return sys_open_vfs_resolved(resolved, ir0_flags, linux_open_flags, mode,
                               dirfd_dbg);
}

/* Open file and return file descriptor */
int64_t sys_open(const char *pathname, int flags, mode_t mode)
{
  char path_copy[256];
  char resolved_path[256];
  int ir0_flags;
  int linux_open_flags = flags;
  size_t plen;
  long len;
  int path_rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;

  /* Validate pathname is in userspace (for USER_MODE processes) */
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  len = strncpy_from_user(path_copy, pathname, sizeof(path_copy));
  if (len < 0 || (size_t)len == sizeof(path_copy))
    return -EFAULT;

  plen = 0;
  while (path_copy[plen] && plen < sizeof(path_copy))
    plen++;
  if (plen == 0 || plen >= sizeof(path_copy))
    return -EINVAL;

  ir0_flags = linux_open_flags_to_ir0(flags);
  ir0_open_flags_log_translation(flags, ir0_flags);
  if (!ir0_open_flags_ok_for_vfs(ir0_flags))
  {
    return -EINVAL;
  }


  path_rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, path_copy, resolved_path,
                                 sizeof(resolved_path), current_process->cwd, current_process->root);
  if (path_rc != 0)
    return path_rc;

  return sys_open_routed_resolved(resolved_path, ir0_flags, linux_open_flags,
                                  mode, IR0_AT_FDCWD);
}

/*
 * sys_openat - Linux openat(2) subset (AT_FDCWD + directory fd).
 */
int64_t sys_openat(int dirfd, const char *pathname, int flags, mode_t mode)
{
  char resolved[256];
  char path_copy[256];
  int ir0_flags;
  int rc;

  if (!current_process)
    return -ESRCH;
  if (!pathname)
    return -EFAULT;
  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  strncpy(path_copy, resolved, sizeof(path_copy) - 1);
  path_copy[sizeof(path_copy) - 1] = '\0';

  ir0_flags = linux_open_flags_to_ir0(flags);
  ir0_open_flags_log_translation(flags, ir0_flags);
  if (!ir0_open_flags_ok_for_vfs(ir0_flags))
  {
    return -EINVAL;
  }

  return sys_open_routed_resolved(resolved, ir0_flags, flags, mode, dirfd);
}

/*
 * fchownat(2)/fchmodat(2) with AT_EMPTY_PATH: operate on the inode referred
 * to by dirfd (OpenRC checkpath uses O_PATH + fchownat(fd, "", …, AT_EMPTY_PATH)).
 */
int fs_resolve_at_empty(int dirfd, char *resolved, size_t resolved_sz)
{
  fd_entry_t *fd_table;

  if (!current_process)
    return -ESRCH;

  if (dirfd == IR0_AT_FDCWD)
  {
    return ir0_resolve_user_path(".", resolved, resolved_sz,
                               current_process->cwd, current_process->root);
  }

  if (dirfd < 0 || dirfd >= MAX_FDS_PER_PROCESS)
    return -EBADF;

  fd_table = get_process_fd_table();
  if (!fd_table || !fd_table[dirfd].in_use || fd_table[dirfd].path[0] == '\0')
    return -EBADF;

  if (strlen(fd_table[dirfd].path) >= resolved_sz)
    return -ENAMETOOLONG;

  memcpy(resolved, fd_table[dirfd].path, strlen(fd_table[dirfd].path) + 1);
  return 0;
}

/*
 * mknod for character/block nodes. Linux gates these behind CAP_MKNOD; IR0
 * has no capabilities yet, so root is the equivalent gate. The node records
 * only the (major,minor): the behaviour comes from the devfs node claiming
 * that number, so an unclaimed one is -ENXIO rather than a file that opens
 * into nothing.
 */
static int64_t mknod_create_device(const char *resolved, unsigned int mode,
                                   unsigned int dev)
{
  if (current_process->euid != ROOT_UID)
    return -EPERM;

  if (!devfs_find_node_by_rdev((uint32_t)dev))
    return -ENXIO;

  return named_devnode_create(resolved,
                              (mode_t)((mode & S_IFMT) | (mode & 07777)),
                              (dev_t)dev);
}

int64_t sys_mknod(const char *pathname, unsigned int mode, unsigned int dev)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if ((mode & S_IFMT) != S_IFIFO &&
      (mode & S_IFMT) != S_IFCHR &&
      (mode & S_IFMT) != S_IFBLK)
    return -ENOSYS;

  if ((mode & S_IFMT) == S_IFIFO && dev != 0)
    return -EINVAL;

  rc = ir0_resolve_user_path(pathname, resolved, sizeof(resolved),
                             current_process->cwd, current_process->root);
  if (rc != 0)
    return rc;

  if ((mode & S_IFMT) != S_IFIFO)
    return mknod_create_device(resolved, mode, dev);

  rc = mknod_prepare_fifo_path(resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  return mknod_create_named_fifo(resolved, (mode_t)(mode & 07777));
}

int64_t sys_mknodat(int dirfd, const char *pathname, unsigned int mode,
		    unsigned int dev)
{
  char resolved[256];
  int rc;

  if (!current_process || !pathname)
    return -EFAULT;

  if (validate_userspace_string(pathname, 256) != 0)
    return -EFAULT;

  if ((mode & S_IFMT) != S_IFIFO &&
      (mode & S_IFMT) != S_IFCHR &&
      (mode & S_IFMT) != S_IFBLK)
    return -ENOSYS;

  if ((mode & S_IFMT) == S_IFIFO && dev != 0)
    return -EINVAL;

  rc = ir0_resolve_path_at(dirfd, pathname, resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  if ((mode & S_IFMT) != S_IFIFO)
    return mknod_create_device(resolved, mode, dev);

  rc = mknod_prepare_fifo_path(resolved, sizeof(resolved));
  if (rc != 0)
    return rc;

  return mknod_create_named_fifo(resolved, (mode_t)(mode & 07777));
}
