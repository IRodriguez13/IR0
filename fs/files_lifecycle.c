/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: files_lifecycle.c
 * Description: files_struct allocation, validation and reference lifecycle.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/process.h>
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <ir0/files_struct.h>
#include <asm/pgtable.h>
#include <ir0/errno.h>
#include <string.h>
#include <ir0/spinlock.h>

static ir0_spinlock_t files_struct_lock;

int files_struct_live(const files_struct_t *f)
{
	uintptr_t p = (uintptr_t)f;
	uintptr_t end;

	if (!f)
		return 0;
	if (p < mm_kernel_heap_start())
		return 0;
	end = p + sizeof(*f);
	if (end < p || end > mm_kernel_heap_start() + mm_kernel_heap_size())
		return 0;
	if (f->magic != IR0_FILES_MAGIC || f->refcount <= 0)
		return 0;
	return 1;
}

files_struct_t *files_create(void)
{
	files_struct_t *f = kmalloc(sizeof(*f));
	fd_entry_t *table;

	if (!f)
		return NULL;
	table = kcalloc((size_t)IR0_FD_INITIAL, sizeof(*table));
	if (!table)
	{
		kfree(f);
		return NULL;
	}
	memset(f, 0, sizeof(*f));
	f->magic = IR0_FILES_MAGIC;
	f->refcount = 1;
	f->max_fds = IR0_FD_INITIAL;
	f->fd_table = table;
	return f;
}

int files_grow(files_struct_t *f, int min_span, int limit)
{
	int n;
	int old;
	fd_entry_t *neu;

	if (!f || min_span <= 0)
		return -EINVAL;
	if (limit > IR0_NOFILE_CEILING)
		limit = IR0_NOFILE_CEILING;
	if (limit <= 0 || min_span > limit)
		return -EMFILE;
	if (f->max_fds >= min_span)
		return 0;

	n = f->max_fds > 0 ? f->max_fds : IR0_FD_INITIAL;
	while (n < min_span)
	{
		if (n > IR0_NOFILE_CEILING / 2)
		{
			n = min_span;
			break;
		}
		n *= 2;
	}
	if (n > limit)
		n = limit;
	if (n < min_span)
		return -EMFILE;

	neu = kcalloc((size_t)n, sizeof(*neu));
	if (!neu)
		return -ENOMEM;
	old = f->max_fds;
	if (f->fd_table && old > 0)
		memcpy(neu, f->fd_table, sizeof(*neu) * (size_t)old);
	kfree(f->fd_table);
	f->fd_table = neu;
	f->max_fds = n;
	return 0;
}

int files_ensure_fd(process_t *p, int fd)
{
	int limit;

	if (!p || !files_struct_live(p->files) || fd < 0)
		return -EBADF;
	limit = process_nofile_limit(p);
	if (fd >= limit)
		return -EBADF;
	return files_grow(p->files, fd + 1, limit);
}

files_struct_t *files_get(files_struct_t *f)
{
	uint64_t irq_flags;

	if (!files_struct_live(f))
		return NULL;
	irq_flags = ir0_spin_lock_irqsave(&files_struct_lock);
	f->refcount++;
	ir0_spin_unlock_irqrestore(&files_struct_lock, irq_flags);
	return f;
}

void files_put(files_struct_t *f)
{
	uint64_t irq_flags;
	int last;

	if (!files_struct_live(f))
		return;
	irq_flags = ir0_spin_lock_irqsave(&files_struct_lock);
	if (f->refcount <= 0)
	{
		ir0_spin_unlock_irqrestore(&files_struct_lock, irq_flags);
		panic("files_put: refcount underflow");
		return;
	}
	f->refcount--;
	last = (f->refcount == 0);
	ir0_spin_unlock_irqrestore(&files_struct_lock, irq_flags);
	if (!last)
		return;
	f->magic = IR0_FILES_MAGIC_DEAD;
	if (f->fd_table && f->max_fds > 0)
	{
		memset(f->fd_table, 0xA5, sizeof(fd_entry_t) * (size_t)f->max_fds);
		kfree(f->fd_table);
		f->fd_table = NULL;
	}
	kfree(f);
}

void process_files_bind(process_t *p, files_struct_t *f)
{
	if (p)
		p->files = f;
}
