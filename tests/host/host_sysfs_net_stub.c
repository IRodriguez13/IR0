/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: host_sysfs_net_stub.c
 * Description: Host-test stubs for /sys/class/net helpers used by pseudo_fs_registry.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sysfs.h>

int sys_class_net_collect_children(const char *dir_path,
				   struct vfs_dirent *entries, int max_entries,
				   int start_n)
{
	(void)dir_path;
	(void)entries;
	(void)max_entries;
	return start_n;
}

int sys_class_net_path_has_children(const char *path)
{
	(void)path;
	return 0;
}
