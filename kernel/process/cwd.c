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
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/process.h>
#include <ir0/syscall_id.h>

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

static int64_t product_cwd_syscall(void *context, enum ir0_syscall_id id,
				   uint64_t a0, uint64_t a1, uint64_t a2,
				   uint64_t a3, uint64_t a4, uint64_t a5)
{
	char buf[256];
	int n;

	(void)context;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	if (id != IR0_SYSCALL_GETCWD)
		return -ENOSYS;
	n = cwd_get(buf, sizeof(buf));
	if (n < 0)
		return n;
	if (a1 < (uint64_t)n)
		return -ERANGE;
	if (copy_to_user((void *)(uintptr_t)a0, buf, (size_t)n) != 0)
		return -EFAULT;
	return n;
}

static const enum ir0_syscall_id g_product_cwd_ids[] = {
	IR0_SYSCALL_GETCWD,
};

const struct syscall_context_provider ir0_product_cwd_provider = {
	.ids = g_product_cwd_ids,
	.count = sizeof(g_product_cwd_ids) / sizeof(g_product_cwd_ids[0]),
	.handler = product_cwd_syscall,
};
