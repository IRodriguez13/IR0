/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — path helpers for syscall entry (userspace path strings).
 */

#ifndef _IR0_PATH_USER_H
#define _IR0_PATH_USER_H

#include <stddef.h>
#include <ir0/utimens.h>

/*
 * ir0_resolve_kpath_at - Resolve kernel-side path against dirfd (AT_FDCWD subset).
 * Absolute paths ignore dirfd. Non-absolute paths require dirfd == IR0_AT_FDCWD.
 */
int ir0_resolve_kpath_at(int dirfd, const char *path, char *resolved,
                         size_t resolved_sz, const char *cwd);

/*
 * ir0_resolve_user_path_at - Copy user path then ir0_resolve_kpath_at().
 */
int ir0_resolve_user_path_at(int dirfd, const char *user_path, char *resolved,
                             size_t resolved_sz, const char *cwd);

/*
 * ir0_resolve_user_path - Copy NUL-terminated user path and resolve against cwd.
 * @user_path: Path pointer from userspace (may be on stack near guard page).
 * @resolved: Kernel buffer for absolute normalized path.
 * @resolved_sz: Size of @resolved.
 * @cwd: Current working directory (kernel string).
 * Returns: 0 on success, -EFAULT / -EINVAL / -ENAMETOOLONG on error.
 */
int ir0_resolve_user_path(const char *user_path, char *resolved, size_t resolved_sz,
                          const char *cwd);

#endif /* _IR0_PATH_USER_H */
