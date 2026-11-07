/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_landlock.c
 * Description: early Landlock ruleset helpers and landlock_* syscalls.
 */

#include "erfs_internal.h"

#include <ir0/early_process.h>

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

int64_t early_rootfs_landlock_create(uint64_t attr, uint64_t size,
				     unsigned flags)
{
	uint64_t handled = 0;
	int slot;
	int fd;
	int idx;

	if (flags)
	{
		if (attr || size)
			return -EINVAL;
		if (flags == IR0_EARLY_LANDLOCK_CREATE_VERSION)
			return IR0_EARLY_LANDLOCK_ABI;
		if (flags == IR0_EARLY_LANDLOCK_CREATE_ERRATA)
			return 0;
		return -EINVAL;
	}
	if (size < 8)
		return -EINVAL;
	if (size > 4096)
		return -E2BIG;
	if (!attr || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&handled, attr, 8) != 0)
		return -EFAULT;
	if (size > 8)
	{
		uint64_t extra;
		uint64_t off;

		for (off = 8; off + 8 <= size && off < 48; off += 8)
		{
			if (g_user->copy_from_user(&extra, attr + off, 8) != 0)
				return -EFAULT;
			if (extra)
				return -EINVAL;
		}
		for (; off < size; off++)
		{
			uint8_t byte;

			if (g_user->copy_from_user(&byte, attr + off, 1) != 0)
				return -EFAULT;
			if (byte)
				return -E2BIG;
		}
	}
	if ((handled | IR0_EARLY_LANDLOCK_FS_MASK) != IR0_EARLY_LANDLOCK_FS_MASK)
		return -EINVAL;
	if (!handled)
		return -ENOMSG;
	for (slot = 0; slot < RULESET_MAX; slot++)
	{
		if (!g_ruleset[slot].used)
			break;
	}
	if (slot >= RULESET_MAX)
		return -EMFILE;
	g_ruleset[slot].used = 1;
	g_ruleset[slot].refs = 1;
	g_ruleset[slot].handled = handled;
	g_ruleset[slot].nrules = 0;
	fd = alloc_fd(IR0_EARLY_ROOTFS_RULESET);
	if (fd < 0)
	{
		g_ruleset[slot].used = 0;
		return fd;
	}
	idx = slot_from_fd(fd);
	if (idx >= 0)
		g_slots[idx].pipe_id = slot;
	set_cloexec_of_fd(fd, 1);
	return fd;
}

int64_t early_rootfs_landlock_add(int fd, int type, uint64_t rule,
				  unsigned flags)
{
	int id;
	uint64_t allowed = 0;
	int32_t parent = 0;
	char pbuf[RAMTAB_NAME];
	int ret;

	if (flags & ~IR0_EARLY_LANDLOCK_ADD_QUIET)
		return -EINVAL;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_RULESET)
		return node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EBADFD;
	id = pipe_id_of_fd(fd);
	if (id < 0 || id >= RULESET_MAX || !g_ruleset[id].used)
		return -EBADFD;
	if (type == IR0_EARLY_LANDLOCK_RULE_NET_PORT)
		return -EAFNOSUPPORT;
	if (type != IR0_EARLY_LANDLOCK_RULE_PATH_BENEATH)
		return -EINVAL;
	if (!rule || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&allowed, rule, 8) != 0 ||
	    g_user->copy_from_user(&parent, rule + 8, 4) != 0)
		return -EFAULT;
	if (!flags && !allowed)
		return -ENOMSG;
	if ((allowed | g_ruleset[id].handled) != g_ruleset[id].handled)
		return -EINVAL;
	if ((flags & IR0_EARLY_LANDLOCK_ADD_QUIET) && !g_ruleset[id].handled)
		return -EINVAL;
	ret = path_of_fd(parent, pbuf, sizeof(pbuf));
	if (ret < 0)
		return ret;
	if (g_ruleset[id].nrules >= LL_RULE_MAX)
		return -E2BIG;
	copy_kstr(g_ruleset[id].rules[g_ruleset[id].nrules].path,
		  sizeof(g_ruleset[id].rules[0].path), pbuf);
	g_ruleset[id].rules[g_ruleset[id].nrules].allowed = allowed;
	g_ruleset[id].nrules++;
	return 0;
}

int64_t early_rootfs_landlock_restrict(int fd, unsigned flags)
{
	int id;
	int i;

	if ((flags | IR0_EARLY_LANDLOCK_RESTRICT_MASK) !=
	    IR0_EARLY_LANDLOCK_RESTRICT_MASK)
		return -EINVAL;
	if (!early_process_is_root())
		return -EPERM;
	if (g_ll_on)
		return -E2BIG;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_RULESET)
		return node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EBADFD;
	id = pipe_id_of_fd(fd);
	if (id < 0 || id >= RULESET_MAX || !g_ruleset[id].used)
		return -EBADFD;
	g_ll_handled = g_ruleset[id].handled;
	g_ll_n = g_ruleset[id].nrules;
	for (i = 0; i < g_ll_n; i++)
	{
		g_ll_rules[i].allowed = g_ruleset[id].rules[i].allowed;
		copy_kstr(g_ll_rules[i].path, sizeof(g_ll_rules[i].path),
			  g_ruleset[id].rules[i].path);
	}
	g_ll_on = 1;
	return 0;
}
