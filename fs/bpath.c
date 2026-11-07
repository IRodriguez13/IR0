/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bpath.c
 * Description: Boot-path fold prefixes for staged erfs → product VFS routing.
 */

#include <ir0/bpath.h>
#include <ir0/bpivot.h>

static int path_eq(const char *path, const char *literal)
{
	unsigned i;

	if (!path || !literal)
		return 0;
	for (i = 0; literal[i] != '\0'; i++)
	{
		if (path[i] != literal[i])
			return 0;
	}
	return path[i] == '\0';
}

int ir0_bpath_is_reserved_early(const char *path)
{
	if (!path || path[0] != '/')
		return 1;
	if (path_eq(path, "/") || path_eq(path, "/init"))
		return 1;
	if (path[1] == 'd' && path[2] == 'e' && path[3] == 'v' &&
	    (path[4] == '\0' || path[4] == '/'))
		return 1;
	if (path[1] == 'b' && path[2] == 'p' && path[3] == 'i' &&
	    path[4] == 'v' && path[5] == 'o' && path[6] == 't' &&
	    (path[7] == '\0' || path[7] == '/'))
		return 1;
	return 0;
}

static int path_has_nontrivial_prefix(const char *path, const char *pref)
{
	unsigned i;

	if (!path || !pref)
		return 0;
	for (i = 0; pref[i] != '\0'; i++)
	{
		if (path[i] != pref[i])
			return 0;
	}
	return path[i] != '\0';
}

static int path_under_bpivot_tree(const char *path)
{
	int i;

	if (ir0_bpivot_mount_count() <= 0)
		return 0;
	for (i = 0; ir0_bpivot_trees[i]; i++)
	{
		const char *m = ir0_bpivot_trees[i];
		char pref[16];
		unsigned j;

		if (path_eq(path, m))
			return 1;
		j = 0;
		while (m[j] != '\0' && j + 2 < sizeof(pref))
		{
			pref[j] = m[j];
			j++;
		}
		pref[j++] = '/';
		pref[j] = '\0';
		if (path_has_nontrivial_prefix(path, pref))
			return 1;
	}
	return 0;
}

int ir0_bpath_is_foldable(const char *path)
{
	if (!path || ir0_bpath_is_reserved_early(path))
		return 0;
	if (ir0_bpivot_mount_count() > 0)
		return path_under_bpivot_tree(path);
	return path_has_nontrivial_prefix(path, IR0_BPATH_TMP_PREF) ||
	       path_has_nontrivial_prefix(path, IR0_BPATH_RUN_PREF);
}

int ir0_bpath_is_tmp(const char *path)
{
	return path_has_nontrivial_prefix(path, IR0_BPATH_TMP_PREF);
}
