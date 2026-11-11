/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fs_dirent.c
 * Description: getdents/getdents64 dirent encoding and scratch buffers.
 */

#include "fs_syscalls_internal.h"

/* Linux getdents (NR 78) — legacy filldir layout: d_type at reclen-1. */
struct linux_dirent {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  char d_name[];
};

#define LINUX_DIRENT_NAME_OFF ((size_t)offsetof(struct linux_dirent, d_name))

/* Linux getdents64 / musl x86_64 struct dirent (NR 217, also IR0 NR 78 for musl). */
struct linux_dirent64 {
  uint64_t d_ino;
  int64_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[];
};

#define LINUX_DIRENT64_NAME_OFF ((size_t)offsetof(struct linux_dirent64, d_name))

/*
 * Keep batch modest: each vfs_dirent is large (path-sized name). 64 × that
 * plus a 4K bounce buffer blew most of IR0_PROC_KSTACK_SIZE and hung/paused
 * guests on ls /proc under GTK (-no-shutdown looked like a freeze).
 */
#define GETDENTS_BATCH_MAX 24
#define GETDENTS_RESOLVED_MAX 512
#define GETDENTS_DOT_PATH_MAX 256

/* Directory entry types (Linux DT_* subset) */
#define DT_UNKNOWN 0
#define DT_FIFO 1
#define DT_CHR 2
#define DT_DIR 4
#define DT_BLK 6
#define DT_REG 8
#define DT_LNK 10
#define DT_SOCK 12

static int getdents_skip_name(const char *name)
{
  size_t name_len;

  if (!name || name[0] == '\0')
    return 1;
  if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
    return 1;
  if ((unsigned char)name[0] <= ' ')
    return 1;
  if (strchr(name, '/'))
    return 1;

  name_len = strlen(name) + 1;
  if (name_len <= 1 || name_len >= 256)
    return 1;

  return 0;
}

static int getdents_trust_readdir_type(const char *dir_path)
{
  if (!dir_path)
    return 0;

  if (strcmp(dir_path, "/proc") == 0 || strcmp(dir_path, "/proc/") == 0)
    return 1;
  if (is_proc_path(dir_path) && proc_is_virtual_subdir(dir_path))
    return 1;

  if (strcmp(dir_path, "/sys") == 0 || strcmp(dir_path, "/sys/") == 0)
    return 1;
  if (is_sys_path(dir_path) && sysfs_is_virtual_subdir(dir_path))
    return 1;

  if (is_heart_path(dir_path) && heart_is_virtual_subdir(dir_path))
    return 1;

  if (strcmp(dir_path, "/dev") == 0 || strcmp(dir_path, "/dev/") == 0)
    return 1;
  if (ir0_is_dev_path(dir_path) && devfs_is_virtual_subdir(dir_path))
    return 1;

  return 0;
}

static void getdents_entry_meta(const char *dir_path, const struct vfs_dirent *ent,
                                int seq, uint64_t *ino, uint8_t *dtype)
{
  char full_path[512];
  size_t path_len;

  *ino = (uint64_t)(seq + 1);
  *dtype = ent->type ? ent->type : DT_REG;

  if (ent->type != 0 || getdents_trust_readdir_type(dir_path))
    return;

  path_len = strlen(dir_path);
  if (path_len > 0 && dir_path[path_len - 1] == '/')
    snprintf(full_path, sizeof(full_path), "%s%s", dir_path, ent->name);
  else
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, ent->name);

  {
    stat_t entry_st;

    if (ir0_stat_path_routed(full_path, &entry_st) >= 0)
    {
      if (entry_st.st_ino)
        *ino = (uint64_t)entry_st.st_ino;
      if (S_ISDIR(entry_st.st_mode))
        *dtype = DT_DIR;
      else if (S_ISREG(entry_st.st_mode))
        *dtype = DT_REG;
      else if (S_ISCHR(entry_st.st_mode))
        *dtype = DT_CHR;
      else if (S_ISBLK(entry_st.st_mode))
        *dtype = DT_BLK;
      else if (S_ISLNK(entry_st.st_mode))
        *dtype = DT_LNK;
      else if (S_ISFIFO(entry_st.st_mode))
        *dtype = DT_FIFO;
    }
  }
}

/*
 * Linux dcache_readdir() emits "." and ".." first; IR0 backends do not list
 * them, so the syscall synthesizes them as the first two cookies.
 */
#define GETDENTS_DOT_ENTRIES 2

static void getdents_dot_meta(const char *dir_path, const stat_t *dir_st,
                              int dotdot, uint64_t *ino, uint8_t *dtype)
{
  char parent[GETDENTS_DOT_PATH_MAX];
  const char *slash;
  stat_t parent_st;

  *dtype = DT_DIR;
  *ino = dir_st->st_ino ? (uint64_t)dir_st->st_ino : 1U;
  if (!dotdot)
    return;

  slash = strrchr(dir_path, '/');
  if (!slash || slash == dir_path ||
      (size_t)(slash - dir_path) >= sizeof(parent))
  {
    parent[0] = '/';
    parent[1] = '\0';
  }
  else
  {
    memcpy(parent, dir_path, (size_t)(slash - dir_path));
    parent[slash - dir_path] = '\0';
  }
  if (ir0_stat_path_routed(parent, &parent_st) >= 0 && parent_st.st_ino)
    *ino = (uint64_t)parent_st.st_ino;
}

static int getdents_visible_count(const struct vfs_dirent *entries, int entry_count)
{
  int visible = 0;

  for (int i = 0; i < entry_count; i++)
  {
    if (getdents_skip_name(entries[i].name))
      continue;
    visible++;
  }

  return visible;
}

/* Linux filldir: ALIGN(name_off + strlen(name) + 2, 8) — +2 = NUL + d_type byte. */
static size_t linux_dirent_reclen(size_t name_len_with_nul)
{
  size_t namlen = name_len_with_nul - 1U;

  return (LINUX_DIRENT_NAME_OFF + namlen + 2U + 7U) & ~((size_t)7U);
}

/* Linux filldir64: ALIGN(name_off + strlen(name) + 1, 8). */
static size_t linux_dirent64_reclen(size_t name_len_with_nul)
{
  size_t namlen = name_len_with_nul - 1U;

  return (LINUX_DIRENT64_NAME_OFF + namlen + 1U + 7U) & ~((size_t)7U);
}

static const char *fs_fd_resolved_dir_path(int fd, char *buf, size_t bufsz)
{
  fd_entry_t *fd_table;
  const char *path;

  fd_table = get_process_fd_table();
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS ||
      !fd_table[fd].in_use)
    return NULL;

  path = fd_table[fd].path;
  if (!path || path[0] == '\0')
  {
    struct vfs_file *vf = (struct vfs_file *)fd_table[fd].vfs_file;

    if (vf && vf->path[0] != '\0')
      path = vf->path;
    else
      return NULL;
  }

  if (is_absolute_path(path))
  {
    if (normalize_path(path, buf, bufsz) != 0)
      return NULL;
    return buf;
  }

  {
    const char *cwd = current_process->cwd;

    if (!cwd || cwd[0] != '/')
      cwd = "/";
    if (strcmp(path, ".") == 0)
    {
      strncpy(buf, cwd, bufsz - 1);
      buf[bufsz - 1] = '\0';
    }
    else if (join_paths(cwd, path, buf, bufsz) != 0)
      return NULL;
  }

  return buf;
}

#define GETDENTS_KBUF_MAX 4096

/*
 * Directory listing scratch, on the heap rather than on the stack.
 *
 * These three buffers made sys_getdents_common's frame 11 KiB. A getdents
 * over virtio-9p reaches hs_readdir and virtio_9p_readdir, each with a 4 KiB
 * frame of its own, so a single `ls` on a 9p directory spent 20 KiB of a
 * 32 KiB kernel stack. An interrupt landing on top of that chain ran into
 * the guard page, and the resulting fault escalated to a double fault: that
 * is what a `find` over the host share hit. Off the stack the chain costs a
 * few hundred bytes and leaves the interrupt room to run.
 */
struct getdents_scratch
{
  char resolved_dir[GETDENTS_RESOLVED_MAX];
  struct vfs_dirent entries[GETDENTS_BATCH_MAX];
  char kernel_buf[GETDENTS_KBUF_MAX];
};

static int64_t sys_getdents_scratch(int fd, void *dirent, size_t count,
                                    int legacy_layout,
                                    struct getdents_scratch *scr);

static int64_t sys_getdents_common(int fd, void *dirent, size_t count, int legacy_layout)
{
  struct getdents_scratch *scr = kmalloc(sizeof(*scr));
  int64_t rc;

  if (!scr)
    return -ENOMEM;

  rc = sys_getdents_scratch(fd, dirent, count, legacy_layout, scr);
  kfree(scr);
  return rc;
}

static int64_t sys_getdents_scratch(int fd, void *dirent, size_t count,
                                    int legacy_layout,
                                    struct getdents_scratch *scr)
{
  fd_entry_t *fd_table;
  char *const resolved_dir = scr->resolved_dir;
  const char *dir_path;
  stat_t st;
  struct vfs_dirent *const entries = scr->entries;
  int entry_count;
  int visible_count;
  size_t start_cookie;
  size_t returned;
  size_t user_budget;
  size_t user_off;
  char *const kernel_buf = scr->kernel_buf;
  size_t buf_offset;
  int64_t copy_size;

  if (!current_process)
    return -ESRCH;
  if (!dirent || count == 0)
    return -EINVAL;

  if (!access_ok(dirent, count))
    return -EFAULT;

  fd_table = get_process_fd_table();
  if (!fd_table || fd < 0 || fd >= MAX_FDS_PER_PROCESS ||
      !fd_table[fd].in_use)
    return -EBADF;

  dir_path = fs_fd_resolved_dir_path(fd, resolved_dir, GETDENTS_RESOLVED_MAX);
  if (!dir_path)
    return -EBADF;

  if (ir0_stat_path_routed(dir_path, &st) < 0)
    return -ENOTDIR;

  if (!S_ISDIR(st.st_mode))
    return -ENOTDIR;

  entry_count = ir0_getdents_path_routed(dir_path, entries, GETDENTS_BATCH_MAX);
  if (entry_count < 0)
    return entry_count;

  visible_count = GETDENTS_DOT_ENTRIES +
                  getdents_visible_count(entries, entry_count);

  start_cookie = (size_t)fd_table[fd].offset;
  if (start_cookie >= (size_t)visible_count)
    return 0;

  returned = 0;
  buf_offset = 0;

  {
    int visible_idx = 0;

    /* i < 0 walks the synthesized "." (-2) and ".." (-1). */
    for (int i = -GETDENTS_DOT_ENTRIES; i < entry_count; i++)
    {
      const char *name;
      size_t name_len;
      size_t reclen;
      uint64_t ino;
      uint8_t dtype;

      if (i < 0)
        name = (i == -GETDENTS_DOT_ENTRIES) ? "." : "..";
      else if (getdents_skip_name(entries[i].name))
        continue;
      else
        name = entries[i].name;

      if ((size_t)visible_idx < start_cookie)
      {
        visible_idx++;
        continue;
      }

      name_len = strlen(name) + 1;
      if (i < 0)
        getdents_dot_meta(dir_path, &st, i == -1, &ino, &dtype);
      else
        getdents_entry_meta(dir_path, &entries[i], visible_idx, &ino, &dtype);

      if (legacy_layout)
        reclen = linux_dirent_reclen(name_len);
      else
        reclen = linux_dirent64_reclen(name_len);

      if (buf_offset + reclen > GETDENTS_KBUF_MAX)
        break;

      if (legacy_layout)
      {
        struct linux_dirent *dent =
            (struct linux_dirent *)(kernel_buf + buf_offset);

        memset(dent, 0, reclen);
        dent->d_ino = ino;
        dent->d_off = (int64_t)(start_cookie + returned + 1);
        dent->d_reclen = (unsigned short)reclen;
        memcpy(dent->d_name, name, name_len);
        ((char *)dent)[reclen - 1U] = (char)dtype;
      }
      else
      {
        struct linux_dirent64 *dent =
            (struct linux_dirent64 *)(kernel_buf + buf_offset);

        memset(dent, 0, reclen);
        dent->d_ino = ino;
        dent->d_off = (int64_t)(start_cookie + returned + 1);
        dent->d_reclen = (unsigned short)reclen;
        dent->d_type = dtype;
        memcpy(dent->d_name, name, name_len);
      }

      buf_offset += reclen;
      returned++;
      visible_idx++;
    }
  }

  if (buf_offset == 0)
    return 0;

  user_budget = count;
  user_off = 0;
  while (user_off < buf_offset)
  {
    unsigned short dent_reclen;
    size_t min_reclen;

    if (legacy_layout)
    {
      struct linux_dirent *dent =
          (struct linux_dirent *)(kernel_buf + user_off);

      dent_reclen = dent->d_reclen;
      min_reclen = LINUX_DIRENT_NAME_OFF + 2U;
    }
    else
    {
      struct linux_dirent64 *dent =
          (struct linux_dirent64 *)(kernel_buf + user_off);

      dent_reclen = dent->d_reclen;
      min_reclen = LINUX_DIRENT64_NAME_OFF + 2U;
    }

    if ((size_t)dent_reclen < min_reclen ||
        user_off + (size_t)dent_reclen > buf_offset)
      return -EIO;
    if ((size_t)dent_reclen > user_budget)
      break;
    user_off += (size_t)dent_reclen;
    user_budget -= (size_t)dent_reclen;
  }

  if (user_off == 0)
    return -EINVAL;

  copy_size = (int64_t)user_off;
  if (copy_to_user(dirent, kernel_buf, (size_t)copy_size) != 0)
    return -EFAULT;

  {
    size_t copied = 0;
    size_t pos = 0;

    while (pos < user_off)
    {
      unsigned short dent_reclen;
      size_t min_reclen;

      if (legacy_layout)
      {
        struct linux_dirent *dent =
            (struct linux_dirent *)(kernel_buf + pos);

        dent_reclen = dent->d_reclen;
        min_reclen = LINUX_DIRENT_NAME_OFF + 2U;
      }
      else
      {
        struct linux_dirent64 *dent =
            (struct linux_dirent64 *)(kernel_buf + pos);

        dent_reclen = dent->d_reclen;
        min_reclen = LINUX_DIRENT64_NAME_OFF + 2U;
      }

      if ((size_t)dent_reclen < min_reclen ||
          pos + (size_t)dent_reclen > user_off)
        break;
      pos += (size_t)dent_reclen;
      copied++;
    }
    fd_table[fd].offset = start_cookie + copied;
  }

  return copy_size;
}

int64_t sys_getdents(int fd, void *dirent, size_t count)
{
  /*
   * musl x86_64 readdir(3) issues SYS_getdents (78) but struct dirent matches
   * linux_dirent64 (d_type before d_name). Emit dirent64 records on NR 78.
   * Legacy filldir layout is reserved for strict Linux NR 78 consumers via
   * getdents64-only paths or future compat toggle.
   */
  return sys_getdents_common(fd, dirent, count, 0);
}

int64_t sys_getdents64(int fd, void *dirent, size_t count)
{
  return sys_getdents_common(fd, dirent, count, 0);
}
