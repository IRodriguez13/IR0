/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — routed pseudo-fs/VFS path helpers for syscall layer.
 */

#ifndef _IR0_PATH_ROUTED_H
#define _IR0_PATH_ROUTED_H

#include <ir0/types.h>
#include <ir0/stat.h>

struct vfs_dirent;

int ir0_is_dev_path(const char *path);
int ir0_stat_path_routed(const char *path, stat_t *st);
int ir0_stat_path_routed_follow(const char *path, stat_t *st);
int ir0_chown_path_routed(const char *path, uid_t owner, gid_t group);
int64_t ir0_access_path_routed(const char *resolved_path, int mode,
                               uid_t euid, gid_t egid);
int64_t ir0_access_path_routed_groups(const char *resolved_path, int mode,
                                      uid_t uid, gid_t gid,
                                      const gid_t *groups, int ngroups);
int ir0_open_access_path_routed(const char *resolved_path, int open_flags,
                                uid_t euid, gid_t egid);
int ir0_getdents_path_routed(const char *path, struct vfs_dirent *entries,
                             int max_entries);

#endif /* _IR0_PATH_ROUTED_H */
