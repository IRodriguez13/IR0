/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ramtab.c
 * Description: Bounded in-memory path table used until a VFS mount owns the name
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/errno.h>
#include <ir0/ramtab.h>
#include <ir0/types.h>

static struct ramtab_ent g_tab[RAMTAB_MAX];
static void (*g_rehome)(int old_id, int new_id);

void ramtab_reset(void)
{
	unsigned i;

	for (i = 0; i < RAMTAB_MAX; i++)
	{
		g_tab[i].used = 0;
		g_tab[i].fifo_pipe = -1;
		g_tab[i].writable = 0;
		g_tab[i].link_to = -1;
		g_tab[i].data = 0;
		g_tab[i].size = 0;
	}
}

void ramtab_set_rehome_observer(void (*fn)(int old_id, int new_id))
{
	g_rehome = fn;
}

int ramtab_eq(const char *left, const char *right)
{
	unsigned i;

	if (!left || !right)
		return left == right;
	for (i = 0; left[i] != '\0' || right[i] != '\0'; i++)
	{
		if (left[i] != right[i])
			return 0;
	}
	return 1;
}

int ramtab_parent(const char *path, char *parent, unsigned cap)
{
	unsigned n = 0;
	unsigned last = 0;
	unsigned i;

	if (!path || !parent || cap < 2 || path[0] != '/')
		return 0;
	while (path[n] != '\0')
	{
		if (path[n] == '/' && n > 0)
			last = n;
		n++;
	}
	if (n <= 1 || last == 0)
	{
		parent[0] = '/';
		parent[1] = '\0';
		return 1;
	}
	if (last + 1 >= cap)
		return 0;
	for (i = 0; i < last; i++)
		parent[i] = path[i];
	parent[last] = '\0';
	return 1;
}

const char *ramtab_basename(const char *path)
{
	unsigned i = 0;
	unsigned last = 0;

	if (!path || path[0] == '\0')
		return path;
	while (path[i] != '\0')
	{
		if (path[i] == '/' && path[i + 1] != '\0')
			last = i + 1;
		i++;
	}
	return path + last;
}

int ramtab_find(const char *path)
{
	unsigned i;

	if (!path)
		return -1;
	for (i = 0; i < RAMTAB_MAX; i++)
	{
		if (g_tab[i].used && ramtab_eq(g_tab[i].path, path))
			return (int)i;
	}
	return -1;
}

struct ramtab_ent *ramtab_get(int idx)
{
	if (idx < 0 || idx >= RAMTAB_MAX || !g_tab[idx].used)
		return 0;
	return &g_tab[idx];
}

struct ramtab_ent *ramtab_table(void)
{
	return g_tab;
}

int ramtab_body(int idx)
{
	int dest;

	if (idx < 0 || idx >= RAMTAB_MAX || !g_tab[idx].used)
		return -1;
	dest = g_tab[idx].link_to;
	if (dest >= 0 && dest < RAMTAB_MAX && dest != idx && g_tab[dest].used)
		return dest;
	return idx;
}

void ramtab_rehome(int fsid)
{
	int i;
	int neu = -1;

	if (fsid < 0 || fsid >= RAMTAB_MAX)
		return;
	for (i = 0; i < RAMTAB_MAX; i++)
	{
		if (i != fsid && g_tab[i].used && g_tab[i].link_to == fsid)
		{
			neu = i;
			break;
		}
	}
	if (neu < 0)
		return;
	g_tab[neu].size = g_tab[fsid].size;
	g_tab[neu].writable = g_tab[fsid].writable;
	g_tab[neu].fifo_pipe = g_tab[fsid].fifo_pipe;
	g_tab[neu].mode = g_tab[fsid].mode;
	if (g_tab[fsid].data == g_tab[fsid].own || g_tab[fsid].writable)
	{
		unsigned n;

		for (n = 0; n < RAMTAB_FILE_CAP; n++)
			g_tab[neu].own[n] = g_tab[fsid].own[n];
		g_tab[neu].data = g_tab[neu].own;
	}
	else
		g_tab[neu].data = g_tab[fsid].data;
	g_tab[neu].link_to = -1;
	for (i = 0; i < RAMTAB_MAX; i++)
	{
		if (g_tab[i].used && g_tab[i].link_to == fsid)
			g_tab[i].link_to = neu;
	}
	if (g_rehome)
		g_rehome(fsid, neu);
}

int ramtab_add(const char *path, uint32_t mode, uint32_t size,
	       const uint8_t *data, int writable)
{
	int idx;
	unsigned i;

	if (!path || path[0] != '/')
		return -EINVAL;
	if (writable && size > RAMTAB_FILE_CAP)
		return -EFBIG;
	idx = ramtab_find(path);
	if (idx >= 0)
	{
		g_tab[idx].mode = mode;
		g_tab[idx].size = size;
		g_tab[idx].writable = 0;
		g_tab[idx].data = data;
		g_tab[idx].link_to = -1;
		return idx;
	}
	for (i = 0; i < RAMTAB_MAX; i++)
	{
		if (g_tab[i].used)
			continue;
		g_tab[i].used = 1;
		for (idx = 0; path[idx] != '\0' && idx + 1 < RAMTAB_NAME; idx++)
			g_tab[i].path[idx] = path[idx];
		if (path[idx] != '\0')
		{
			g_tab[i].used = 0;
			return -ENAMETOOLONG;
		}
		g_tab[i].path[idx] = '\0';
		g_tab[i].mode = mode;
		g_tab[i].size = size;
		g_tab[i].fifo_pipe = -1;
		g_tab[i].link_to = -1;
		g_tab[i].writable = writable;
		if (writable)
		{
			unsigned n;

			g_tab[i].data = g_tab[i].own;
			for (n = 0; n < RAMTAB_FILE_CAP; n++)
				g_tab[i].own[n] = 0;
			if (data && size)
			{
				for (n = 0; n < size; n++)
					g_tab[i].own[n] = data[n];
			}
		}
		else
			g_tab[i].data = data;
		return (int)i;
	}
	return -ENOSPC;
}

enum ir0_early_rootfs_node ramtab_node_kind(uint32_t mode)
{
	uint32_t type = mode & S_IFMT;

	if (type == S_IFDIR)
		return IR0_EARLY_ROOTFS_USERDIR;
	if (type == S_IFIFO)
		return IR0_EARLY_ROOTFS_FIFO;
	if (type == S_IFSOCK)
		return IR0_EARLY_ROOTFS_UNIX;
	if (type == S_IFLNK)
		return IR0_EARLY_ROOTFS_SYMLINK;
	return IR0_EARLY_ROOTFS_FILE;
}

int ramtab_child_count(const char *dir)
{
	char parent[RAMTAB_NAME];
	unsigned i;
	int n = 0;

	for (i = 0; i < RAMTAB_MAX; i++)
	{
		if (!g_tab[i].used)
			continue;
		if (!ramtab_parent(g_tab[i].path, parent, sizeof(parent)))
			continue;
		if (ramtab_eq(parent, dir) && !ramtab_eq(g_tab[i].path, dir))
			n++;
	}
	return n;
}

int ramtab_child_at(const char *dir, int which,
		    struct ir0_early_dirent_spec *spec)
{
	char parent[RAMTAB_NAME];
	unsigned i;
	int n = 0;

	if (!spec)
		return 0;
	for (i = 0; i < RAMTAB_MAX; i++)
	{
		uint32_t type;

		if (!g_tab[i].used)
			continue;
		if (!ramtab_parent(g_tab[i].path, parent, sizeof(parent)))
			continue;
		if (!ramtab_eq(parent, dir) || ramtab_eq(g_tab[i].path, dir))
			continue;
		if (n == which)
		{
			type = g_tab[i].mode & S_IFMT;
			spec->name = ramtab_basename(g_tab[i].path);
			spec->ino = 16 + i;
			if (type == S_IFDIR)
				spec->type = IR0_EARLY_DT_DIR;
			else if (type == S_IFIFO)
				spec->type = IR0_EARLY_DT_FIFO;
			else if (type == S_IFSOCK)
				spec->type = IR0_EARLY_DT_SOCK;
			else if (type == S_IFLNK)
				spec->type = IR0_EARLY_DT_LNK;
			else
				spec->type = IR0_EARLY_DT_REG;
			return 1;
		}
		n++;
	}
	return 0;
}
