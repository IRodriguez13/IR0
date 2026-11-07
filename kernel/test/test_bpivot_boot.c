/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_bpivot_boot.c
 * Description: Ktest for portable bpivot product boot on common VFS.
 */

#include "test/ktest_harness.h"
#include "test/ktest_decl.h"

#include <ir0/bpath.h>
#include <ir0/bpivot.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>
#include <string.h>

void ktest_bpivot_boot_contract(void)
{
	const struct ir0_bpath_ops *ops;
	stat_t st;
	int ret;

	KTEST_BEGIN("bpivot_boot_contract");

	ops = ir0_bpath_vfs_product_ops();
	KASSERT(ops != NULL);

	ret = ir0_bpivot_product_boot(ops);
	KASSERT_EQ(ret, 0);
	KASSERT(ir0_bpivot_committed());
	KASSERT(ir0_bpivot_mount_count() >= ir0_bpivot_tree_count() / 2);
	KASSERT(ir0_bpivot_mount_count() >= 2);
	KASSERT(bpath_live());
	KASSERT(ir0_bpath_is_foldable("/tmp/fold"));
	KASSERT_EQ(vfs_stat("/tmp/fold", &st), 0);
	KASSERT(S_ISDIR(st.st_mode));
	KASSERT_EQ(vfs_stat(IR0_BPIVOT_NEW_ROOT, &st), 0);
	KASSERT(S_ISDIR(st.st_mode));
	KASSERT_EQ(vfs_stat(IR0_BPIVOT_PUT_OLD, &st), 0);
	KASSERT(S_ISDIR(st.st_mode));

	KTEST_END();
}
