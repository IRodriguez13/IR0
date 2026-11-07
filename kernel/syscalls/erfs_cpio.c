/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_cpio.c
 * Description: Linux newc CPIO ingest — ramtab (staged) and product /tmp fold.
 */

#include "erfs_internal.h"

#include <ir0/early_cpio.h>
#include <ir0/product_pivot.h>
#include <ir0/vfs_file.h>

const uint8_t *g_cpio_buf;
uint64_t g_cpio_len;
int g_cpio_product_installed;

static int cpio_install_ramtab(const char *norm, uint32_t mode, uint32_t size,
			       const uint8_t *data)
{
	if (ramtab_add(norm, mode, size, data, 0) < 0)
		return -1;
	return 0;
}

static int cpio_install_product_tmp(const char *norm, uint32_t mode,
				    uint32_t size, const uint8_t *data)
{
	struct vfs_file *vf;
	int rc;

	if (!product_path_live() || !ir0_product_path_is_foldable(norm) ||
	    !g_product_path)
		return 0;
	if ((mode & S_IFMT) == S_IFDIR)
	{
		rc = g_product_path->mkdir ? g_product_path->mkdir(norm, mode & 07777U) : -ENOSYS;
		return rc < 0 ? rc : 1;
	}
	if ((mode & S_IFMT) != S_IFREG)
		return 0;
	if (!g_product_path->open || !g_product_path->put)
		return -ENOSYS;
	rc = g_product_path->open(norm, IR0_O_RDWR | IR0_O_CREAT | IR0_O_TRUNC,
				  mode & 07777U, &vf);
	if (rc < 0)
		return rc;
	if (size > 0 && data && g_product_path->write)
	{
		size_t wrote = 0;

		while (wrote < size)
		{
			size_t chunk = size - wrote;

			if (chunk > 4096U)
				chunk = 4096U;
			rc = (int)g_product_path->write(vf, data + wrote, chunk);
			if (rc < 0)
			{
				(void)g_product_path->put(vf);
				return rc;
			}
			wrote += (size_t)rc;
		}
	}
	(void)g_product_path->put(vf);
	g_product_path_used = 1;
	return 1;
}

int erfs_cpio_find_init(const uint8_t *buf, uint64_t bytes,
			const uint8_t **init_data, uint64_t *init_len)
{
	uint64_t off = 0;
	int saw_file = 0;

	if (init_data)
		*init_data = 0;
	if (init_len)
		*init_len = 0;
	if (!buf || bytes < IR0_EARLY_CPIO_HEADER)
		return -EINVAL;
	while (1)
	{
		struct ir0_early_cpio_member mem;
		char norm[RAMTAB_NAME];
		int ret;

		ret = ir0_early_cpio_next(buf, bytes, off, &mem);
		if (ret <= 0)
			break;
		off = mem.next;
		if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
			continue;
		saw_file = 1;
		if ((mem.mode & S_IFMT) == S_IFREG &&
		    ir0_early_rootfs_path_eq(norm, "/init") &&
		    ir0_early_elf64_image_bytes(mem.data, mem.filesize))
		{
			if (init_data)
				*init_data = mem.data;
			if (init_len)
				*init_len = mem.filesize;
		}
	}
	if (!init_data || !*init_data || !init_len || !*init_len)
		return saw_file ? -ENOEXEC : -EINVAL;
	return 0;
}

int erfs_cpio_install_members(const uint8_t *buf, uint64_t bytes)
{
	uint64_t off = 0;
	int count = 0;

	if (!buf)
		return -EINVAL;
	while (1)
	{
		struct ir0_early_cpio_member mem;
		char norm[RAMTAB_NAME];
		int ret;
		int prod;

		ret = ir0_early_cpio_next(buf, bytes, off, &mem);
		if (ret <= 0)
			break;
		off = mem.next;
		if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
			continue;
		prod = cpio_install_product_tmp(norm, mem.mode, mem.filesize,
						mem.data);
		if (prod > 0)
		{
			g_cpio_product_installed++;
			count++;
			continue;
		}
		if (prod < 0)
			return prod;
		if (cpio_install_ramtab(norm, mem.mode, mem.filesize, mem.data) < 0)
			return -ENOSPC;
		count++;
	}
	return count;
}

int early_rootfs_cpio_fold_product(void)
{
	uint64_t off = 0;

	if (!g_cpio_ready || !g_cpio_buf || g_cpio_len == 0)
		return -ENOENT;
	if (!product_path_live())
		return -ENODEV;
	g_cpio_product_installed = 0;
	while (1)
	{
		struct ir0_early_cpio_member mem;
		char norm[RAMTAB_NAME];
		int ret;
		int prod;

		ret = ir0_early_cpio_next(g_cpio_buf, g_cpio_len, off, &mem);
		if (ret <= 0)
			break;
		off = mem.next;
		if (!ir0_early_cpio_normalize(mem.name, norm, sizeof(norm)))
			continue;
		prod = cpio_install_product_tmp(norm, mem.mode, mem.filesize,
						mem.data);
		if (prod > 0)
			g_cpio_product_installed++;
		else if (prod < 0)
			return prod;
	}
	if (g_cpio_product_installed > 0)
		ir0_product_pivot_note_cpio_fold(g_cpio_product_installed);
	return g_cpio_product_installed > 0 ? g_cpio_product_installed :
					      -ENOENT;
}

int early_rootfs_product_pivot_done(void)
{
	return ir0_product_pivot_cpio_fold_done();
}
