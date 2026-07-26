/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pseudo_snapshot.h
 * Description: Contract for finite pseudo-file snapshots (proc/sys/heart).
 *
 * Snapshot semantics (default for /proc,/sys,/heart text nodes):
 *   open → generate content (or regenerate per read into bounce)
 *   read → respect process fd offset; return 0 at EOF
 *   lseek → SEEK_SET/CUR/END within snapshot size when bound in fd_table
 *   close → release bind refs once
 *
 * Implementation today: pseudo_fs_ops_read() regenerates into a 4KiB bounce
 * buffer then slices by *offset (see fs/pseudo_fs_registry.c). Generators
 * may ignore offset. True streams (kmsg ring, future event logs) must use
 * a dedicated ops->read that advances an opaque cursor and document the
 * stream contract explicitly.
 *
 * dup/fork: share the same bind + offset (Linux fd semantics).
 * Independent open(): independent offset / regenerated view.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

/*
 * Conceptual helpers — the registry bounce path is the shared implementation.
 * Prefer registering generators via pseudo_fs_register + pseudo_fs_ops_read
 * rather than inventing per-node offset math.
 */
typedef int (*pseudo_snapshot_generator_t)(char *buf, size_t count);

static inline int64_t pseudo_snapshot_slice(const char *full, size_t full_len,
					     char *buf, size_t count,
					     off_t *offset)
{
	size_t to_copy;

	if (!full || !buf || !offset)
		return -1;
	if (*offset < 0)
		return -1;
	if ((size_t)*offset >= full_len)
		return 0;
	to_copy = full_len - (size_t)*offset;
	if (to_copy > count)
		to_copy = count;
	for (size_t i = 0; i < to_copy; i++)
		buf[i] = full[(size_t)*offset + i];
	*offset += (off_t)to_copy;
	return (int64_t)to_copy;
}
