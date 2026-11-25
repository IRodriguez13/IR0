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
 * then dropped. The cache belongs to the file's inode (inode->i_mapping)
 * and holds a reference on it, so every name of a file shares one cache
 * and a mapped file outlives its names.
 */

struct inode;

/*
 * Set up (or extend) the cache of @inode for file pages
 * [pgoff, pgoff + npages) and return its id in @id. @writable: a mapping
 * may store into the pages, so they are written back. The caller holds
 * vfs_pagecache_lock() until the pages are mapped.
 */
int filemap_map_file(struct inode *inode, uint64_t pgoff, size_t npages,
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
 * VFS hooks, called under the fs-op lock after a backend read, write or
 * truncate of @inode's file succeeded; a NULL @inode has no cache.
 */
void filemap_sync_read(struct inode *inode, void *buf, size_t done,
		       off_t offset);
void filemap_sync_write(struct inode *inode, const void *buf, size_t done,
			off_t offset);
void filemap_sync_truncate(struct inode *inode, size_t length);
/* Any cache exists: I/O by name must look for the file's inode. */
int filemap_active(void);
