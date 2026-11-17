/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fat16_fs.c
 * Description: The "fat16" filesystem type: a FAT16 volume on a block device.
 */

#include "fat16_fs.h"
#include "fat16_disk.h"
#include <fs/vfs.h>

static struct vfs_ops fat16_ops = {
	.stat = fat16_disk_stat,
	.mkdir = fat16_disk_mkdir,
	.create = fat16_disk_create,
	.unlink = fat16_disk_unlink,
	.rmdir = fat16_disk_rmdir,
	.link = fat16_disk_link,
	.chown = fat16_disk_chown,
	.chmod = fat16_disk_chmod,
	.readdir = fat16_disk_readdir,
	.read = fat16_disk_read,
	.write = fat16_disk_write,
	.truncate = fat16_disk_truncate,
};

static struct vfs_fstype fat16_fs_type = {
	.name = "fat16",
	.ops = &fat16_ops,
	.mount = fat16_disk_mount,
	.umount = fat16_disk_umount,
	.next = NULL,
};

int fat16_fs_register(void)
{
	return vfs_register_fs(&fat16_fs_type);
}
