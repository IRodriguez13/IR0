/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — path helpers for syscall entry (userspace path strings).
 */

#ifndef _IR0_PATH_USER_H
#define _IR0_PATH_USER_H

#include <stddef.h>
#include <ir0/utimens.h>

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
