/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host stub: bpivot mounts are not exercised in unit tests.
 */

#include <ir0/bpivot.h>
#include <ir0/errno.h>

const char *const ir0_bpivot_trees[] = {
	"/tmp",
	"/run",
	"/usr",
	NULL,
};

int ir0_bpivot_mount_count(void)
{
	return 0;
}

int ir0_bpivot_cpio_fold_done(void)
{
	return 0;
}

int ir0_bpivot_prepare_staging(void)
{
	return -ENODEV;
}

int ir0_bpivot_commit(void)
{
	return -ENODEV;
}

int ir0_bpivot_committed(void)
{
	return 0;
}

const struct ir0_bpath_ops *ir0_bpath_vfs_product_ops(void)
{
	return NULL;
}

int ir0_bpivot_product_boot(const struct ir0_bpath_ops *ops)
{
	(void)ops;
	return -ENODEV;
}
