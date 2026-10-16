/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_file.c
 * Description: Portable open-file description (Linux struct file).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <ir0/fd_resource.h>
#include <ir0/kmem.h>
#include <ir0/vfs_file.h>
#include <stdint.h>
#include <string.h>

_Static_assert(sizeof(off_t) == sizeof(uint64_t),
	       "vfs_file.pos is the 64-bit cursor used by fd pos_of_fd");

static void vfs_file_copy_path(char *dst, size_t dst_size, const char *src)
{
	size_t i;

	if (!dst || dst_size == 0)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	for (i = 0; src[i] != '\0' && i + 1 < dst_size; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

struct vfs_file *vfs_file_alloc(const char *path, int flags)
{
	struct vfs_file *f;

	f = kmalloc_try(sizeof(*f));
	if (!f)
		return NULL;
	memset(f, 0, sizeof(*f));
	vfs_file_copy_path(f->path, sizeof(f->path), path);
	f->pos = 0;
	f->flags = flags;
	f->ref_count = 1;
	return f;
}

void vfs_file_acquire(struct vfs_file *f)
{
	unsigned long irq_flags;

	if (!f)
		return;
	irq_flags = irq_save();
	f->ref_count++;
	irq_restore(irq_flags);
}

int vfs_file_put_last(struct vfs_file *f, void (*on_last)(struct vfs_file *f))
{
	unsigned long irq_flags;
	int last;

	if (!f)
		return -EBADF;
	irq_flags = irq_save();
	if (f->ref_count > 0)
		f->ref_count--;
	last = (f->ref_count <= 0);
	irq_restore(irq_flags);
	if (!last)
		return 0;
	if (on_last)
		on_last(f);
	kfree(f);
	return 0;
}

int vfs_file_put(struct vfs_file *f)
{
	return vfs_file_put_last(f, NULL);
}

static int vfs_file_resource_acquire(fd_entry_t *entry)
{
	if (!entry || !entry->vfs_file)
		return -EINVAL;
	vfs_file_acquire((struct vfs_file *)entry->vfs_file);
	return 0;
}

static void vfs_file_resource_release(fd_entry_t *entry)
{
	if (!entry || !entry->vfs_file)
		return;
	(void)vfs_file_put((struct vfs_file *)entry->vfs_file);
}

static const fd_resource_ops_t g_vfs_file_resource_ops = {
	"vfs_file",
	vfs_file_resource_acquire,
	vfs_file_resource_release,
};

static const struct ir0_vfs_file_ops g_vfs_file_ops = {
	.alloc = vfs_file_alloc,
	.acquire = vfs_file_acquire,
	.put = vfs_file_put,
};

const struct ir0_vfs_file_ops *ir0_vfs_file_product(void)
{
	return &g_vfs_file_ops;
}

const struct fd_resource_ops *ir0_vfs_file_resource_ops(void)
{
	return &g_vfs_file_resource_ops;
}
