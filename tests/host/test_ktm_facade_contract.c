/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_ktm_facade_contract.c
 * Description: Host test — KTM sources must not include <mm/...> directly.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int ktm_tree_has_mm_include(const char *dir_path)
{
	DIR *dir;
	struct dirent *ent;
	char path[512];
	FILE *f;
	char line[512];
	int bad = 0;

	dir = opendir(dir_path);
	if (!dir)
		return -1;

	while ((ent = readdir(dir)) != NULL)
	{
		size_t namelen;

		if (ent->d_name[0] == '.')
			continue;
		namelen = strlen(ent->d_name);
		if (namelen >= 2 && ent->d_name[namelen - 2] == '.'
		    && ent->d_name[namelen - 1] == 'c')
		{
			snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
		}
		else if (ent->d_type == DT_DIR)
		{
			snprintf(path, sizeof(path), "%s/%s", dir_path, ent->d_name);
			if (ktm_tree_has_mm_include(path) != 0)
				bad = 1;
			continue;
		}
		else
		{
			continue;
		}

		f = fopen(path, "r");
		if (!f)
		{
			bad = 1;
			continue;
		}
		while (fgets(line, (int)sizeof(line), f))
		{
			if (strstr(line, "#include <mm/") || strstr(line, "#include \"mm/"))
			{
				fprintf(stderr, "KTM mm include: %s: %s", path, line);
				bad = 1;
				break;
			}
		}
		fclose(f);
	}

	closedir(dir);
	return bad ? 1 : 0;
}

void test_ktm_facade_no_mm_includes(void)
{
	char ktm_root[512];
	const char *root;
	int rc;

	TEST_BEGIN("ktm_facade_no_mm_includes");
	root = getenv("IR0_KERNEL_ROOT");
	if (!root || !root[0])
		root = "../..";
	snprintf(ktm_root, sizeof(ktm_root), "%s/ktm", root);
	rc = ktm_tree_has_mm_include(ktm_root);
	ASSERT_EQ(rc, 0);
	TEST_END();
}
