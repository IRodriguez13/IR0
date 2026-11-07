/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bpivot.c
 * Description: Boot pivot tmpfs mounts, pivot_root staging, ramtab commit.
 */

#include <ir0/bpivot.h>
#include <ir0/bpath.h>
#include <ir0/errno.h>
#include <ir0/ramtab.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>

#include "tmpfs.h"
#include <string.h>

static int g_bpivot_mounts;
static int g_bpivot_cpio_fold;
static int g_bpivot_staging;
static int g_bpivot_committed;
static int g_bpivot_root_switched;

const char *const ir0_bpivot_trees[] = {
	"/bpivot",
	"/tmp",
	"/run",
	"/usr",
	"/etc",
	"/bin",
	"/sbin",
	"/var",
	"/lib",
	"/opt",
	"/home",
	"/root",
	"/srv",
	"/mnt",
	NULL,
};

static int bpivot_ensure_mountpoint(const char *path)
{
	stat_t st;

	if (!path || path[0] != '/')
		return -EINVAL;
	if (vfs_stat(path, &st) == 0)
		return 0;
	return vfs_mkdir(path, 0755);
}

int ir0_bpivot_mount_trees(void)
{
	int i;
	int ok = 0;

	/*
	 * vfs_init() may have registered tmpfs already; -EEXIST is success here.
	 */
	(void)tmpfs_register();
	for (i = 0; ir0_bpivot_trees[i]; i++)
	{
		const char *tree = ir0_bpivot_trees[i];
		int ret;

		ret = bpivot_ensure_mountpoint(tree);
		if (ret != 0 && ret != -EEXIST)
			continue;
		ret = vfs_mount("none", tree, "tmpfs");
		if (ret == 0 || ret == -EBUSY)
			ok++;
	}
	g_bpivot_mounts = ok;
	return ok;
}

int ir0_bpivot_mount_count(void)
{
	return g_bpivot_mounts;
}

void ir0_bpivot_note_cpio_fold(int installed)
{
	if (installed > 0 && g_bpivot_mounts > 0)
		g_bpivot_cpio_fold = installed;
}

int ir0_bpivot_cpio_fold_done(void)
{
	return g_bpivot_cpio_fold > 0;
}

int ir0_bpivot_tree_count(void)
{
	int n = 0;

	while (ir0_bpivot_trees[n])
		n++;
	return n;
}

int ir0_bpivot_prepare_staging(void)
{
	stat_t st;
	int ret;

	if (g_bpivot_staging)
		return 0;
	if (vfs_stat("/bpivot", &st) != 0)
		return -ENOENT;
	if (vfs_mkdir(IR0_BPIVOT_NEW_ROOT, 0755) != 0)
	{
		if (vfs_stat(IR0_BPIVOT_NEW_ROOT, &st) != 0)
			return -EIO;
	}
	ret = vfs_mount("none", IR0_BPIVOT_NEW_ROOT, "tmpfs");
	if (ret != 0 && ret != -EBUSY)
		return -EIO;
	if (vfs_mkdir(IR0_BPIVOT_PUT_OLD, 0755) != 0)
	{
		if (vfs_stat(IR0_BPIVOT_PUT_OLD, &st) != 0)
			return -EIO;
	}
	g_bpivot_staging = 1;
	return 0;
}

int ir0_bpivot_stage_path(const char *path, char *out, size_t out_size)
{
	size_t root_len;
	size_t path_len;

	if (!path || !out || path[0] != '/' || !g_bpivot_staging ||
	    g_bpivot_root_switched)
		return -EINVAL;
	root_len = sizeof(IR0_BPIVOT_NEW_ROOT) - 1;
	path_len = strlen(path);
	if (root_len + path_len + 1 > out_size)
		return -ENAMETOOLONG;
	memcpy(out, IR0_BPIVOT_NEW_ROOT, root_len);
	memcpy(out + root_len, path, path_len + 1);
	return 0;
}

static int bpivot_ramtab_drop_cb(const char *path)
{
	const struct ir0_bpath_ops *ops;
	char staged[VFS_PATH_MAX];
	stat_t st;

	if (!ir0_bpath_is_foldable(path))
		return 0;
	ops = ir0_bpath_bound();
	if (!ops || !ops->stat)
		return 0;
	if (ir0_bpivot_stage_path(path, staged, sizeof(staged)) != 0)
		return 0;
	return ops->stat(staged, &st) == 0;
}

static int bpivot_staging_valid(void)
{
	stat_t st;

	if (vfs_stat(IR0_BPIVOT_NEW_ROOT, &st) != 0)
		return 0;
	if (!S_ISDIR(st.st_mode))
		return 0;
	if (vfs_stat(IR0_BPIVOT_PUT_OLD, &st) != 0)
		return 0;
	return S_ISDIR(st.st_mode);
}

int ir0_bpivot_commit(void)
{
	int pruned;

	if (g_bpivot_committed)
		return 0;
	if (g_bpivot_mounts < 2)
		return -ENODEV;
	if (ir0_bpivot_prepare_staging() < 0)
		return -EIO;
	if (!bpivot_staging_valid())
		return -EIO;
	if (!ir0_bpivot_cpio_fold_done())
		return -EINVAL;
	pruned = ramtab_prune_matching(bpivot_ramtab_drop_cb);
	if (pruned < 0)
		return pruned;
	if (vfs_pivot_root(IR0_BPIVOT_NEW_ROOT, IR0_BPIVOT_PUT_OLD) != 0)
		return -EIO;
	g_bpivot_root_switched = 1;
	g_bpivot_committed = 1;
	return 0;
}

int ir0_bpivot_committed(void)
{
	return g_bpivot_committed;
}

int ir0_bpivot_root_switched(void)
{
	return g_bpivot_root_switched;
}
