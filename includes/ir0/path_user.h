/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — path helpers for syscall entry (userspace path strings).
 */

#ifndef _IR0_PATH_USER_H
#define _IR0_PATH_USER_H

#include <stddef.h>
#include <ir0/utimens.h>

/*
 * copy_path_from_user - Copy a NUL-terminated path from userspace, the IR0
 * analogue of Linux getname().
 * @dst: kernel buffer of @size bytes
 * @user_path: userspace source
 * @size: capacity of @dst, including the NUL
 *
 * Returns: the path length (without the NUL); -EFAULT on a bad or unmapped
 * source; -ENAMETOOLONG when no NUL fits in @size bytes.
 */
long copy_path_from_user(char *dst, const char *user_path, size_t size);

/*
 * ir0_resolve_kpath_at - Resolve kernel-side path against dirfd + chroot.
 * Absolute paths: mapped under @root (host-absolute jail).
 * Relative paths: join @cwd (AT_FDCWD only); not forced under root (Linux).
 * @root: process chroot (NULL or "/" = no jail).
 */
int ir0_resolve_kpath_at(int dirfd, const char *path, char *resolved,
                         size_t resolved_sz, const char *cwd,
                         const char *root);

/*
 * ir0_resolve_user_path_at - Copy user path then ir0_resolve_kpath_at().
 */
int ir0_resolve_user_path_at(int dirfd, const char *user_path, char *resolved,
                             size_t resolved_sz, const char *cwd,
                             const char *root);

/*
 * ir0_resolve_user_path - Copy NUL-terminated user path and resolve against
 * cwd + chroot root.
 */
int ir0_resolve_user_path(const char *user_path, char *resolved,
                          size_t resolved_sz, const char *cwd,
                          const char *root);

#endif /* _IR0_PATH_USER_H */
