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
#include <ir0/swap.h>
#include <ir0/time.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

struct vfs_dirent;
struct super_block;

/*
 * Operations on a file named by its inode number (Linux inode_operations
 * and file_operations of a regular file), for a backend that can address
 * its files that way. They are what an open file uses, so a file keeps
 * working after it lost its names. @sb is the mounted filesystem
 * (s_fs_info is the backend's own state). Same conventions as the path
 * operations below. @evict frees a file whose link count is 0 when the VFS
 * drops its last reference (see inode_orphan() in <ir0/inode.h>).
 */
struct vfs_inode_ops {
	int (*stat)(struct super_block *sb, ino_t ino, stat_t *buf);
	int (*read)(struct super_block *sb, ino_t ino, void *buf, size_t count,
		    size_t *bytes_read, off_t offset);
	int (*write)(struct super_block *sb, ino_t ino, const void *buf,
		     size_t count, size_t *bytes_written, off_t offset);
	int (*truncate)(struct super_block *sb, ino_t ino, size_t length);
	void (*evict)(struct super_block *sb, ino_t ino);
};

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
	 * Optional open(2) check, called by vfs_open() after the permission
	 * checks with the IR0_O_* flags; a negative errno fails the open.
	 */
	int (*open)(const char *path, int flags);
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
	/*
	 * utimensat(2) backend: set access/modification times.
	 * times == NULL means "now" for both. Backends with a single on-disk
	 * timestamp (MINIX v1) store the modification time and report it for
	 * both st_atim and st_mtim.
	 */
	int (*utimens)(const char *path, const struct timespec times[2]);
	/*
	 * Pin a preallocated, non-sparse file as swap backing. Backends that
	 * cannot map stable physical extents leave this NULL (swapon → -EOPNOTSUPP).
	 */
	int (*map_swap_extents)(const char *path, struct ir0_swap_extent *out,
				size_t max, size_t *count, dev_t *dev);
	/* Inode-addressed operations, or NULL (files are named by path). */
	const struct vfs_inode_ops *iops;
};

/*
 * VFS path helpers (implemented in fs/vfs*.c) — syscalls must use these,
 * never backend-specific minix_* or fat_* symbols.
 *
 * vfs_pread / vfs_pwrite: explicit offset; does not advance vfs_file->pos.
 * vfs_truncate(path, length): permission-checked truncate.
 * vfs_stat: delegates to ops->stat; metadata must reflect on-disk/in-memory
 * state after truncate and close (no stale inode cache).
 * vfs_rename: link + unlink composition when ops->link is present.
 * vfs_readdir: maps to getdents-style directory listing (DT_DIR/DT_REG).
 */
