/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: filemap.c
 * Description: Page cache of MAP_SHARED file mappings (Linux mm/filemap.c).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/filemap.h>
#include <ir0/inode.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/mm_alloc.h>
#include <ir0/mm_port.h>
#include <ir0/page.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/spinlock.h>
#include <ir0/vfs.h>
#include <string.h>

#define FM_PAGE ((size_t)IR0_MM_PAGE_SIZE)

/*
 * One file's cache. frames[i] holds file page i, or 0 when it is not
 * resident; every page below the file size is resident once it falls in a
 * mapped range. The cache holds one reference on each frame; each page
 * table entry that maps it holds another.
 */
struct filemap
{
	uint64_t id;
	struct inode *inode; /* the file; the cache holds one reference */
	int writable; /* a mapping may store: write back */
	size_t size;  /* file size as the cache sees it */
	size_t npages;
	uintptr_t *frames;
	struct filemap *next;
};

/*
 * The caches. filemap_lock guards the list, every field above and the
 * inodes' i_mapping; it is the only lock the page fault path takes here. Everything that changes a
 * cache's size, name or frame table, or frees it, also holds the fs-op lock
 * (vfs_pagecache_lock), so code holding that lock may use a cache across
 * backend I/O, which runs without filemap_lock.
 */
static struct filemap *filemap_list;
static uint64_t filemap_last_id;
static ir0_spinlock_t filemap_lock;

static size_t fm_pages(size_t bytes)
{
	return bytes / FM_PAGE + (bytes % FM_PAGE != 0);
}

static uint8_t *fm_page_va(uintptr_t phys)
{
	return (uint8_t *)phys_to_virt(phys);
}

/* The cache of @inode, or NULL. filemap_lock held. */
static struct filemap *fm_of(const struct inode *inode)
{
	return inode ? inode->i_mapping : NULL;
}

/* filemap_lock held. */
static struct filemap *fm_by_id(uint64_t id)
{
	struct filemap *fm;

	for (fm = filemap_list; fm; fm = fm->next)
	{
		if (fm->id == id)
			return fm;
	}
	return NULL;
}

/* Zero the cached bytes at file offsets >= @from. filemap_lock held. */
static void fm_zero_from(struct filemap *fm, size_t from)
{
	for (size_t i = from / FM_PAGE; i < fm->npages; i++)
	{
		size_t start = (i == from / FM_PAGE) ? from % FM_PAGE : 0;

		if (fm->frames[i])
			memset(fm_page_va(fm->frames[i]) + start, 0, FM_PAGE - start);
	}
}

/* Grow the frame table to @need slots. fs-op lock held. */
static int fm_reserve(struct filemap *fm, size_t need)
{
	uintptr_t *slots;
	uintptr_t *old;
	unsigned long flags;

	if (need <= fm->npages)
		return 0;
	slots = kcalloc(need, sizeof(*slots));
	if (!slots)
		return -ENOMEM;
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	if (fm->npages)
		memcpy(slots, fm->frames, fm->npages * sizeof(*slots));
	old = fm->frames;
	fm->frames = slots;
	fm->npages = need;
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	kfree(old);
	return 0;
}

/*
 * Read in the pages of [first, last) that lie inside the file and are not
 * resident. The backend is current for them: nothing cached them yet.
 * fs-op lock held.
 */
static int fm_fill(struct filemap *fm, size_t first, size_t last)
{
	if (last > fm->npages)
		last = fm->npages;
	for (size_t i = first; i < last; i++)
	{
		size_t off = i * FM_PAGE;
		size_t want;
		size_t done = 0;
		uintptr_t phys;
		unsigned long flags;

		if (fm->frames[i] || off >= fm->size)
			continue;
		phys = mm_alloc_frame(MM_ALLOC_DIRECT_RECLAIM);
		if (!phys)
			return -ENOMEM;
		paging_zero_phys_page(phys);
		want = fm->size - off;
		if (want > FM_PAGE)
			want = FM_PAGE;
		if (vfs_pagecache_read(fm->inode, fm_page_va(phys), want,
				       (off_t)off, &done) != 0)
		{
			pmm_free_frame(phys);
			return -EIO;
		}
		flags = ir0_spin_lock_irqsave(&filemap_lock);
		fm->frames[i] = phys;
		ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	}
	return 0;
}

int filemap_map_file(struct inode *inode, uint64_t pgoff, size_t npages,
		     int writable, uint64_t *id)
{
	struct filemap *fm;
	unsigned long flags;
	size_t size = 0;
	size_t end;
	int ret;

	if (!inode || !id || npages == 0)
		return -EINVAL;
	if (pgoff > SIZE_MAX / FM_PAGE || npages > SIZE_MAX / FM_PAGE - pgoff)
		return -EOVERFLOW;
	end = (size_t)pgoff + npages;

	vfs_pagecache_lock();
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_of(inode);
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	if (!fm)
	{
		ret = vfs_pagecache_size(inode, &size);
		if (ret != 0)
			goto out;
		fm = kcalloc(1, sizeof(*fm));
		if (!fm)
		{
			ret = -ENOMEM;
			goto out;
		}
		ihold(inode);
		fm->inode = inode;
		fm->size = size;
		flags = ir0_spin_lock_irqsave(&filemap_lock);
		fm->id = ++filemap_last_id;
		fm->next = filemap_list;
		filemap_list = fm;
		inode->i_mapping = fm;
		ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	}
	/* A cache that ends up unused is dropped by the next reclaim. */
	ret = fm_reserve(fm, end);
	if (ret == 0)
		ret = fm_fill(fm, (size_t)pgoff, end);
	if (ret == 0)
	{
		if (writable)
			fm->writable = 1;
		*id = fm->id;
	}
out:
	vfs_pagecache_unlock();
	return ret;
}

int filemap_get_page(uint64_t id, uint64_t index, uintptr_t *phys)
{
	struct filemap *fm;
	unsigned long flags;
	int ret = -EFAULT;

	if (!phys)
		return -EINVAL;
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_by_id(id);
	if (fm && index < fm->npages && index < fm_pages(fm->size) &&
	    fm->frames[index])
	{
		pmm_frame_get(fm->frames[index]);
		*phys = fm->frames[index];
		ret = 0;
	}
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	return ret;
}

/* Write the resident pages inside the file to the backend. fs-op lock held. */
static void fm_write_back(struct filemap *fm)
{
	size_t size;

	if (!fm->writable)
		return;
	/* Never past the end the backend has: the cache does not extend it. */
	if (vfs_pagecache_size(fm->inode, &size) != 0)
		return;
	if (size > fm->size)
		size = fm->size;
	for (size_t i = 0; i < fm->npages && i * FM_PAGE < size; i++)
	{
		size_t n = size - i * FM_PAGE;
		size_t done = 0;

		if (!fm->frames[i])
			continue;
		if (n > FM_PAGE)
			n = FM_PAGE;
		(void)vfs_pagecache_write(fm->inode, fm_page_va(fm->frames[i]),
					  n, (off_t)(i * FM_PAGE), &done);
	}
}

int filemap_writeback(uint64_t id)
{
	struct filemap *fm;
	unsigned long flags;

	vfs_pagecache_lock();
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_by_id(id);
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	if (fm)
		fm_write_back(fm);
	vfs_pagecache_unlock();
	return 0;
}

/* No page table maps a frame of @fm. filemap_lock held. */
static int fm_unmapped(const struct filemap *fm)
{
	for (size_t i = 0; i < fm->npages; i++)
	{
		if (fm->frames[i] && pmm_frame_refcount(fm->frames[i]) > 1)
			return 0;
	}
	return 1;
}

void filemap_reclaim(void)
{
	if (!filemap_list)
		return;
	vfs_pagecache_lock();
	for (;;)
	{
		struct filemap **link;
		struct filemap *fm = NULL;
		unsigned long flags;

		flags = ir0_spin_lock_irqsave(&filemap_lock);
		for (link = &filemap_list; *link; link = &(*link)->next)
		{
			if (fm_unmapped(*link))
			{
				fm = *link;
				*link = fm->next;
				fm->inode->i_mapping = NULL;
				break;
			}
		}
		ir0_spin_unlock_irqrestore(&filemap_lock, flags);
		if (!fm)
			break;
		fm_write_back(fm);
		for (size_t i = 0; i < fm->npages; i++)
		{
			if (fm->frames[i])
				pmm_free_frame(fm->frames[i]);
		}
		kfree(fm->frames);
		iput(fm->inode);
		kfree(fm);
	}
	vfs_pagecache_unlock();
}

void filemap_sync_read(struct inode *inode, void *buf,
		       size_t done, off_t offset)
{
	struct filemap *fm;
	unsigned long flags;
	size_t pos;
	size_t copied = 0;

	if (!filemap_list || !buf || done == 0 || offset < 0)
		return;
	pos = (size_t)offset;
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_of(inode);
	while (fm && copied < done)
	{
		size_t idx = (pos + copied) / FM_PAGE;
		size_t in_page = (pos + copied) % FM_PAGE;
		size_t chunk = FM_PAGE - in_page;

		if (chunk > done - copied)
			chunk = done - copied;
		if (idx < fm->npages && fm->frames[idx])
			memcpy((uint8_t *)buf + copied,
			       fm_page_va(fm->frames[idx]) + in_page, chunk);
		copied += chunk;
	}
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
}

void filemap_sync_write(struct inode *inode, const void *buf,
			size_t done, off_t offset)
{
	struct filemap *fm;
	unsigned long flags;
	size_t pos;
	size_t end;
	size_t old;
	size_t copied = 0;

	if (!filemap_list || !buf || done == 0 || offset < 0)
		return;
	pos = (size_t)offset;
	end = pos + done;
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_of(inode);
	if (!fm)
	{
		ir0_spin_unlock_irqrestore(&filemap_lock, flags);
		return;
	}
	old = fm->size;
	if (end > old)
	{
		/* Stores past the old end never reach the file (Linux zeroes them). */
		fm_zero_from(fm, old);
		fm->size = end;
	}
	while (copied < done)
	{
		size_t idx = (pos + copied) / FM_PAGE;
		size_t in_page = (pos + copied) % FM_PAGE;
		size_t chunk = FM_PAGE - in_page;

		if (chunk > done - copied)
			chunk = done - copied;
		if (idx < fm->npages && fm->frames[idx])
			memcpy(fm_page_va(fm->frames[idx]) + in_page,
			       (const uint8_t *)buf + copied, chunk);
		copied += chunk;
	}
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	if (end > old)
		(void)fm_fill(fm, old / FM_PAGE, fm_pages(end));
}

void filemap_sync_truncate(struct inode *inode, size_t length)
{
	struct filemap *fm;
	unsigned long flags;
	size_t old;

	if (!filemap_list)
		return;
	flags = ir0_spin_lock_irqsave(&filemap_lock);
	fm = fm_of(inode);
	if (!fm)
	{
		ir0_spin_unlock_irqrestore(&filemap_lock, flags);
		return;
	}
	old = fm->size;
	/* Shrinking drops the tail; growing shows zeros past the old end. */
	fm_zero_from(fm, length < old ? length : old);
	fm->size = length;
	ir0_spin_unlock_irqrestore(&filemap_lock, flags);
	if (length > old)
		(void)fm_fill(fm, old / FM_PAGE, fm_pages(length));
}

int filemap_active(void)
{
	return filemap_list != NULL;
}
