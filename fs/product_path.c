/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: product_path.c
 * Description: Product VFS fold path prefixes for staged erfs routing.
 */

#include <ir0/product_path.h>

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

int ir0_product_path_is_foldable(const char *path)
{
	return path_has_nontrivial_prefix(path, IR0_PRODUCT_PATH_TMP_PREF) ||
	       path_has_nontrivial_prefix(path, IR0_PRODUCT_PATH_RUN_PREF);
}

int ir0_product_path_is_tmp(const char *path)
{
	return path_has_nontrivial_prefix(path, IR0_PRODUCT_PATH_TMP_PREF);
}
