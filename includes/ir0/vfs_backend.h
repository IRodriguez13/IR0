/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_backend.h
 * Description: VFS ↔ filesystem backend contract (see docs/fs-vfs-contract.md).
 */

#pragma once

#include <ir0/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

struct vfs_dirent;

/*
 * Every block/filesystem driver registers a struct vfs_ops table.
 * VFS dispatches path-based operations; backends must not depend on
 * syscalls, process state, or workload-specific harness code.
 *
 * Return convention: 0 on success, negative errno on failure.
 * Partial I/O: on success, *bytes_* out-parameters hold bytes transferred
 * (may be less than count at EOF). Return value of vfs_read/vfs_write is
 * the byte count; backend read/write return 0 and set *bytes_*.
 */

struct vfs_ops {
	int (*stat)(const char *path, stat_t *buf);
	int (*mkdir)(const char *path, mode_t mode);
	int (*create)(const char *path, mode_t mode);
	int (*unlink)(const char *path);
	int (*rmdir)(const char *path);
	int (*link)(const char *oldpath, const char *newpath);
	/*
	 * Atomic rename within the same mount when the backend supports it
	 * (e.g. 9P TRENAMEAT). If NULL, vfs_rename falls back to link+unlink.
	 */
	int (*rename)(const char *oldpath, const char *newpath);
	int (*symlink)(const char *target, const char *linkpath);
	/*
	 * Copy symlink target into @buf (NUL-terminated). Return bytes that
	 * would be needed excluding NUL (POSIX readlink style), or -errno.
	 */
	int (*readlink)(const char *path, char *buf, size_t buflen);
	int (*chown)(const char *path, uid_t owner, gid_t group);
	int (*chmod)(const char *path, mode_t mode);
	int (*readdir)(const char *path, struct vfs_dirent *entries, int max);
	/*
	 * Offset-aware read (POSIX pread semantics at backend level).
	 * offset >= file size: *bytes_read = 0, return 0 (EOF).
	 * offset < 0: -EINVAL.
	 * Directory: -EISDIR. Missing path: -ENOENT.
	 */
	int (*read)(const char *path, void *buf, size_t count,
		    size_t *bytes_read, off_t offset);
	/*
	 * Offset-aware write (POSIX pwrite semantics at backend level).
	 * May extend file if offset + count > st_size (sparse hole fill with
	 * zero bytes between old size and offset is backend-defined; tmpfs
	 * zero-fills, MINIX writes zero blocks).
	 * offset < 0: -EINVAL.
	 */
	int (*write)(const char *path, const void *buf, size_t count,
		     size_t *bytes_written, off_t offset);
	/*
	 * Truncate regular file to length bytes. length <= current size.
	 * Growing truncate: -ENOSYS unless backend supports it.
	 * Directory: -EISDIR. length > max file size: -EFBIG.
	 */
	int (*truncate)(const char *path, size_t length);
};

/*
 * VFS path helpers (implemented in fs/vfs.c) — syscalls must use these,
 * never backend-specific minix_* or fat_* symbols.
 *
 * vfs_pread / vfs_pwrite: explicit offset; does not advance vfs_file->pos.
 * vfs_truncate(path, length): permission-checked truncate.
 * vfs_stat: delegates to ops->stat; metadata must reflect on-disk/in-memory
 * state after truncate and close (no stale inode cache).
 * vfs_rename: link + unlink composition when ops->link is present.
 * vfs_readdir: maps to getdents-style directory listing (DT_DIR/DT_REG).
 */
