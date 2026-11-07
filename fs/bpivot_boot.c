/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bpivot_boot.c
 * Description: Portable product boot pivot (bpath bind, CPIO fold, commit).
 */

#include <ir0/bpath.h>
#include <ir0/bpivot.h>
#include <ir0/boot_log.h>
#include <ir0/early_rootfs.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>

static int bpath_vfs_read(struct vfs_file *f, void *buf, size_t n)
{
	return vfs_read(f, (char *)buf, n);
}

static int bpath_vfs_write(struct vfs_file *f, const void *buf, size_t n)
{
	return vfs_write(f, (const char *)buf, n);
}

static const struct ir0_bpath_ops g_vfs_bpath_ops = {
	.stat = vfs_stat,
	.open = vfs_open,
	.read = bpath_vfs_read,
	.write = bpath_vfs_write,
	.lseek = vfs_lseek,
	.put = vfs_file_put,
	.mkdir = vfs_mkdir,
	.unlink = vfs_unlink,
	.rmdir = vfs_rmdir,
	.truncate = vfs_truncate,
};

const struct ir0_bpath_ops *ir0_bpath_vfs_product_ops(void)
{
	return &g_vfs_bpath_ops;
}

int ir0_bpivot_product_boot(const struct ir0_bpath_ops *ops)
{
	int fold;
	int ret;

	if (!ops)
		return -EINVAL;
	if (ir0_bpivot_mount_count() <= 0)
	{
		if (ir0_bpivot_mount_trees() < 2)
			return -ENODEV;
	}
	if (ir0_bpivot_mount_count() < 2)
		return -ENODEV;

	ir0_boot_smoke("IR0_BPIVOT_MOUNT_OK");
	early_rootfs_bind_bpath(ops);

	if (vfs_mkdir("/tmp/fold", 0755) == 0 &&
	    ir0_bpath_is_foldable("/tmp/fold"))
		ir0_boot_smoke("IR0_BPATH_FOLD_OK");
	if (vfs_mkdir("/run/fold", 0755) == 0 &&
	    ir0_bpath_is_foldable("/run/fold"))
		ir0_boot_smoke("IR0_BPATH_RUN_FOLD_OK");

	if (early_rootfs_cpio_ready())
	{
		fold = early_rootfs_cpio_bpath_fold();
		if (fold > 0)
			ir0_boot_smoke("IR0_INITRAMFS_CPIO_OK");
	}

	ret = ir0_bpivot_commit();
	if (ret == 0)
	{
		ir0_boot_smoke("IR0_BPIVOT_STAGING_OK");
		ir0_boot_smoke("IR0_BPIVOT_COMMIT_OK");
	}
	return ret;
}
