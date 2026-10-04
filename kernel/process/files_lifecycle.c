/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: files_lifecycle.c
 * Description: files_struct allocation, validation and reference lifecycle.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/files_struct.h>
#include <mm/allocator.h>
#include <string.h>

int files_struct_live(const files_struct_t *f)
{
	uintptr_t p = (uintptr_t)f;
	uintptr_t end;

	if (!f)
		return 0;
	if (p < (uintptr_t)SIMPLE_HEAP_START)
		return 0;
	end = p + sizeof(*f);
	if (end < p || end > (uintptr_t)SIMPLE_HEAP_END)
		return 0;
	if (f->magic != IR0_FILES_MAGIC || f->refcount <= 0)
		return 0;
	return 1;
}

files_struct_t *files_create(void)
{
	files_struct_t *f = kmalloc_try(sizeof(*f));

	if (!f)
		return NULL;
	memset(f, 0, sizeof(*f));
	f->magic = IR0_FILES_MAGIC;
	f->refcount = 1;
	return f;
}

files_struct_t *files_get(files_struct_t *f)
{
	uint64_t irq_flags;

	if (!files_struct_live(f))
		return NULL;
	irq_flags = process_irq_save();
	f->refcount++;
	process_irq_restore(irq_flags);
	return f;
}

void files_put(files_struct_t *f)
{
	uint64_t irq_flags;
	int last;

	if (!files_struct_live(f))
		return;
	irq_flags = process_irq_save();
	if (f->refcount <= 0)
	{
		process_irq_restore(irq_flags);
		panic("files_put: refcount underflow");
		return;
	}
	f->refcount--;
	last = (f->refcount == 0);
	process_irq_restore(irq_flags);
	if (!last)
		return;
	f->magic = IR0_FILES_MAGIC_DEAD;
	memset(f->fd_table, 0xA5, sizeof(f->fd_table));
	kfree(f);
}

void process_files_bind(process_t *p, files_struct_t *f)
{
	if (p)
		p->files = f;
}
