/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — utimensat(2) helpers (Linux ABI subset for musl/BusyBox).
 */

#ifndef _IR0_UTIMENS_H
#define _IR0_UTIMENS_H

#include <ir0/time.h>

/* Linux uapi/fcntl.h */
#define IR0_AT_FDCWD        (-100)
#define IR0_AT_SYMLINK_NOFOLLOW 0x100
#define IR0_AT_EACCESS      0x200

/* Linux uapi/linux/time.h */
#define IR0_UTIME_NOW       ((1L << 30) - 1L)
#define IR0_UTIME_OMIT      ((1L << 30) - 2L)

/*
 * ir0_utimensat_path - Apply utimensat semantics on a resolved absolute path.
 * @resolved: Absolute normalized path in kernel memory.
 * @times: Two-element timespec array in kernel memory, or NULL for "now".
 * @flags: AT_SYMLINK_NOFOLLOW etc.
 * Returns: 0, -ENOENT, -EINVAL, -ENOSYS, or other negative errno.
 */
int ir0_utimensat_path(const char *resolved, const struct timespec times[2],
                       int flags);

#endif /* _IR0_UTIMENS_H */
