/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * SPDX-License-Identifier: GPL-3.0-only
 *
 * cwd/root owners. Linux reads current->fs->pwd; IR0 reads
 * current_process->cwd / current_process->root.
 */

#include <ir0/cwd.h>
#include <ir0/errno.h>
#include <ir0/process.h>

static int cwd_copy_out(char *dst, size_t dst_size, const char *src)
{
	size_t n;
	size_t i;

	if (!dst || dst_size == 0 || !src)
		return -EINVAL;
	n = 0;
	while (src[n] != '\0')
		n++;
	n++;
	if (n > dst_size)
		return -ERANGE;
	for (i = 0; i < n; i++)
		dst[i] = src[i];
	return (int)n;
}

static void cwd_copy_in(char *dst, size_t dst_size, const char *src)
{
	size_t i;

	if (!dst || dst_size == 0)
		return;
	if (!src || src[0] != '/')
	{
		dst[0] = '/';
		dst[1] = '\0';
		return;
	}
	for (i = 0; src[i] != '\0' && i + 1 < dst_size; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static int cwd_get(char *buf, size_t size)
{
	if (!current_process)
		return -ESRCH;
	if (current_process->cwd[0] != '/')
	{
		current_process->cwd[0] = '/';
		current_process->cwd[1] = '\0';
	}
	return cwd_copy_out(buf, size, current_process->cwd);
}

static int cwd_set(const char *path)
{
	if (!current_process)
		return -ESRCH;
	cwd_copy_in(current_process->cwd, sizeof(current_process->cwd), path);
	return 0;
}

static int cwd_get_root(char *buf, size_t size)
{
	if (!current_process)
		return -ESRCH;
	if (current_process->root[0] != '/')
	{
		current_process->root[0] = '/';
		current_process->root[1] = '\0';
	}
	return cwd_copy_out(buf, size, current_process->root);
}

static int cwd_set_root(const char *path)
{
	if (!current_process)
		return -ESRCH;
	cwd_copy_in(current_process->root, sizeof(current_process->root), path);
	return 0;
}

static const struct ir0_cwd_ops g_cwd_ops = {
	.get = cwd_get,
	.set = cwd_set,
	.get_root = cwd_get_root,
	.set_root = cwd_set_root,
};

const struct ir0_cwd_ops *ir0_cwd_product(void)
{
	return &g_cwd_ops;
}
