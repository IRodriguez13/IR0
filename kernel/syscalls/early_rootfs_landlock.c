/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_rootfs_landlock.c
 * Description: early Landlock ruleset helpers (staged rootfs path).
 */

#include "early_rootfs_internal.h"

void copy_kstr(char *dst, unsigned cap, const char *src)
{
	unsigned i;

	if (!dst || !cap)
		return;
	if (!src)
	{
		dst[0] = '\0';
		return;
	}
	for (i = 0; src[i] != '\0' && i + 1 < cap; i++)
		dst[i] = src[i];
	dst[i] = '\0';
}

static int path_is_beneath(const char *path, const char *parent)
{
	unsigned i = 0;

	if (!path || !parent || parent[0] == '\0' || path[0] != '/')
		return 0;
	if (parent[0] == '/' && parent[1] == '\0')
		return 1;
	while (parent[i] && path[i] == parent[i])
		i++;
	if (parent[i] != '\0')
		return 0;
	return path[i] == '\0' || path[i] == '/';
}

int landlock_allow(const char *path, uint64_t need)
{
	uint64_t want;
	uint64_t got = 0;
	int i;

	if (!g_ll_on || !need || !path)
		return 0;
	want = need & g_ll_handled;
	if (!want)
		return 0;
	for (i = 0; i < g_ll_n; i++)
	{
		if (path_is_beneath(path, g_ll_rules[i].path))
			got |= g_ll_rules[i].allowed;
	}
	if ((got & want) == want)
		return 0;
	return -EACCES;
}

int landlock_open_need(const char *path, int flags)
{
	char look[RAMTAB_NAME];
	enum ir0_early_rootfs_node node;
	uint64_t need = 0;
	int acc = flags & LINUX_O_ACCMODE;

	copy_kstr(look, sizeof(look), path);
	node = lookup_user_path(look);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return 0;
	if (node_is_dir(node) || (flags & LINUX_O_DIRECTORY))
		need |= IR0_EARLY_LANDLOCK_FS_READ_DIR;
	else
	{
		if (acc != 1)
			need |= IR0_EARLY_LANDLOCK_FS_READ_FILE;
		if (acc != 0)
			need |= IR0_EARLY_LANDLOCK_FS_WRITE_FILE;
	}
	if (flags & LINUX_O_TRUNC)
		need |= IR0_EARLY_LANDLOCK_FS_TRUNCATE;
	return landlock_allow(path, need);
}
