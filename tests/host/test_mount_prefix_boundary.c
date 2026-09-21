/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_mount_prefix_boundary.c
 * Description: Host test — mount prefix boundary (root "/" vs nested mounts).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test_harness_ir0.h"
#include <ir0/mount_prefix.h>
#include <string.h>

static int simulate_longest_match(const char *path,
				  const char *const mounts[], size_t n_mounts,
				  size_t *best_len_out)
{
	size_t best = 0;
	size_t i;

	if (!path || !best_len_out)
		return -1;

	for (i = 0; i < n_mounts; i++)
	{
		const char *mount = mounts[i];
		size_t mlen;

		if (!mount)
			continue;
		mlen = strlen(mount);
		if (strncmp(path, mount, mlen) != 0)
			continue;
		if (!ir0_mount_prefix_boundary_ok(path, mount, mlen))
			continue;
		if (mlen >= best)
			best = mlen;
	}

	*best_len_out = best;
	return best > 0 ? 0 : -1;
}

void test_mount_prefix_boundary(void)
{
	const char *root_only[] = { "/" };
	const char *nested[] = { "/", "/mnt/ext2" };
	size_t best;

	TEST_BEGIN("mount_prefix_root_slash");
	ASSERT(ir0_mount_prefix_boundary_ok("/", "/", 1));
	ASSERT(ir0_mount_prefix_boundary_ok("/sbin/init", "/", 1));
	ASSERT(ir0_mount_prefix_boundary_ok("/etc/passwd", "/", 1));
	ASSERT(ir0_mount_prefix_boundary_ok("/mnt/foo", "/mnt", 4));
	ASSERT(!ir0_mount_prefix_boundary_ok("/mntfoo", "/mnt", 4));
	ASSERT(!ir0_mount_prefix_boundary_ok("/mntx", "/mnt", 4));
	TEST_END();

	TEST_BEGIN("mount_prefix_longest_match");
	ASSERT_EQ(simulate_longest_match("/sbin/init", root_only, 1, &best), 0);
	ASSERT_EQ(best, 1U);
	ASSERT_EQ(simulate_longest_match("/mnt/ext2/file", nested, 2, &best), 0);
	ASSERT_EQ(best, 9U);
	ASSERT_EQ(simulate_longest_match("/mnt/ext2", nested, 2, &best), 0);
	ASSERT_EQ(best, 9U);
	/* "/mnt/ext2extra" matches nested prefix but fails boundary; "/" still matches. */
	ASSERT_EQ(simulate_longest_match("/mnt/ext2extra", nested, 2, &best), 0);
	ASSERT_EQ(best, 1U);
	TEST_END();
}
