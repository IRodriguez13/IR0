/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: product_pivot.c
 * Description: Tmpfs staging mounts and pivot state for erfs → product VFS.
 */

#include <ir0/product_pivot.h>
#include <ir0/vfs.h>

#include "tmpfs.h"

static int g_product_pivot_mounts;
static int g_product_pivot_cpio_fold;

const char *const ir0_product_pivot_trees[] = {
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

int ir0_product_pivot_mount_trees(void)
{
	int i;
	int ok = 0;

	if (tmpfs_register() != 0)
		return 0;
	for (i = 0; ir0_product_pivot_trees[i]; i++)
	{
		if (vfs_mount("none", ir0_product_pivot_trees[i], "tmpfs") == 0)
			ok++;
	}
	g_product_pivot_mounts = ok;
	return ok;
}

int ir0_product_pivot_mount_count(void)
{
	return g_product_pivot_mounts;
}

void ir0_product_pivot_note_cpio_fold(int installed)
{
	if (installed > 0 && g_product_pivot_mounts > 0)
		g_product_pivot_cpio_fold = installed;
}

int ir0_product_pivot_cpio_fold_done(void)
{
	return g_product_pivot_cpio_fold > 0;
}

int ir0_product_pivot_tree_count(void)
{
	int n = 0;

	while (ir0_product_pivot_trees[n])
		n++;
	return n;
}
