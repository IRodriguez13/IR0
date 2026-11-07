/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host stub: pivot mounts are not exercised in unit tests.
 */

#include <ir0/product_pivot.h>

const char *const ir0_product_pivot_trees[] = {
	"/tmp",
	"/run",
	"/usr",
	NULL,
};

int ir0_product_pivot_mount_count(void)
{
	return 0;
}

int ir0_product_pivot_cpio_fold_done(void)
{
	return 0;
}
