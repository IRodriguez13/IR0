/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: filemap.h
 * Description: Page cache of MAP_SHARED file mappings (Linux mm/filemap.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

/*
 * A file mapped MAP_SHARED gets one page cache: the pages every mapping of
 * it shares. While it exists it is the file's contents: read() returns its
 * pages, write() and truncate() update them, so mappings and file I/O see
 * the same bytes. Pages inside the file size are always resident; access
 * past the end raises SIGBUS. The cache is written back to the filesystem
 * by msync() and when its last page is unmapped (munmap, exit, exec), and
 * then dropped. A file is named as the VFS names it to its backend
 * (filesystem handle, path), see vfs_pagecache_key().
 */

/*
 * Set up (or extend) the cache of @path for file pages
 * [pgoff, pgoff + npages) and return its id in @id. @writable: a mapping
 * may store into the pages, so they are written back. The caller holds
 * vfs_pagecache_lock() until the pages are mapped.
 */
int filemap_map_file(const char *path, uint64_t pgoff, size_t npages,
		     int writable, uint64_t *id);

/*
 * Take a reference on file page @index of cache @id for a page table
 * entry: 0 and the frame in @phys, or -EFAULT when the page lies past the
 * end of the file or the cache is gone (SIGBUS).
 */
int filemap_get_page(uint64_t id, uint64_t index, uintptr_t *phys);

/* msync(): write the cache back to the filesystem. */
int filemap_writeback(uint64_t id);

/*
 * Write back and drop every cache no page table maps any more. Called
 * after pages are unmapped (munmap, the last mm_put).
 */
void filemap_reclaim(void);

/*
 * VFS hooks, called under the fs-op lock after the backend op succeeded,
 * with the (filesystem, path) pair that op used.
 */
void filemap_sync_read(const void *fs, const char *key, void *buf,
		       size_t done, off_t offset);
void filemap_sync_write(const void *fs, const char *key, const void *buf,
			size_t done, off_t offset);
void filemap_sync_truncate(const void *fs, const char *key, size_t length);
void filemap_forget(const void *fs, const char *key);
void filemap_rename(const void *fs, const char *oldkey, const char *newkey);
