/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: saved_environ.c
 * Description: Exec-time environ/cmdline cache for /proc/<pid>/environ and cmdline.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "process_internal.h"
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <string.h>

#define PROCESS_ENVIRON_MAX (128U * 1024U)
#define PROCESS_ENVIRON_VARS_MAX 256
#define PROCESS_CMDLINE_MAX (64U * 1024U)
#define PROCESS_CMDLINE_ARGS_MAX 256

static int process_saved_nul_blob_set(char **dst, size_t *dst_len,
				      char *const items[], unsigned max_vars,
				      size_t max_bytes)
{
	size_t total = 0;
	int count = 0;

	if (!dst || !dst_len)
		return -EINVAL;

	if (*dst)
	{
		kfree(*dst);
		*dst = NULL;
	}
	*dst_len = 0;

	if (!items)
		return 0;

	while (items[count])
	{
		size_t len = strlen(items[count]) + 1;

		if (count >= (int)max_vars || total + len > max_bytes)
			return -E2BIG;
		total += len;
		count++;
	}

	if (total == 0)
		return 0;

	*dst = kmalloc_try(total);
	if (!*dst)
		return -ENOMEM;

	total = 0;
	for (int i = 0; i < count; i++)
	{
		size_t len = strlen(items[i]) + 1;

		memcpy(*dst + total, items[i], len);
		total += len;
	}
	*dst_len = total;
	return 0;
}

void process_saved_environ_clear(process_t *p)
{
	if (!p)
		return;

	if (p->saved_environ)
	{
		kfree(p->saved_environ);
		p->saved_environ = NULL;
	}
	p->saved_environ_len = 0;
}

int process_saved_environ_set(process_t *p, char *const envp[])
{
	if (!p)
		return -EINVAL;

	process_saved_environ_clear(p);
	return process_saved_nul_blob_set(&p->saved_environ, &p->saved_environ_len,
					  envp, PROCESS_ENVIRON_VARS_MAX,
					  PROCESS_ENVIRON_MAX);
}

int process_saved_environ_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	process_saved_environ_clear(dst);
	if (!src->saved_environ || src->saved_environ_len == 0)
		return 0;

	dst->saved_environ = kmalloc_try(src->saved_environ_len);
	if (!dst->saved_environ)
		return -ENOMEM;

	memcpy(dst->saved_environ, src->saved_environ, src->saved_environ_len);
	dst->saved_environ_len = src->saved_environ_len;
	return 0;
}

void process_saved_cmdline_clear(process_t *p)
{
	if (!p)
		return;

	if (p->saved_cmdline)
	{
		kfree(p->saved_cmdline);
		p->saved_cmdline = NULL;
	}
	p->saved_cmdline_len = 0;
}

int process_saved_cmdline_set(process_t *p, char *const argv[])
{
	if (!p)
		return -EINVAL;

	process_saved_cmdline_clear(p);
	return process_saved_nul_blob_set(&p->saved_cmdline, &p->saved_cmdline_len,
					  argv, PROCESS_CMDLINE_ARGS_MAX,
					  PROCESS_CMDLINE_MAX);
}

int process_saved_cmdline_clone(process_t *dst, const process_t *src)
{
	if (!dst || !src)
		return -EINVAL;

	process_saved_cmdline_clear(dst);
	if (!src->saved_cmdline || src->saved_cmdline_len == 0)
		return 0;

	dst->saved_cmdline = kmalloc_try(src->saved_cmdline_len);
	if (!dst->saved_cmdline)
		return -ENOMEM;

	memcpy(dst->saved_cmdline, src->saved_cmdline, src->saved_cmdline_len);
	dst->saved_cmdline_len = src->saved_cmdline_len;
	return 0;
}
