/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sysfs.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - SYSFS (System Filesystem) Header
 * Copyright (C) 2025  Iván Rodriguez
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <ir0/stat.h>
#include <ir0/types.h>
#include <ir0/vfs.h>

/* /sys filesystem interface; files are pseudo_fs nodes (pseudo_fs_nodes.c). */
bool is_sys_path(const char *path);
int sysfs_stat(const char *path, stat_t *st);
int sysfs_is_virtual_subdir(const char *path);

/* kernel/utsname_info.c reads the hostname and the online CPU count. */
int sys_kernel_hostname_read_reg(char *buf, size_t count);
/* Count of CPUs currently marked online in sysfs (at least 1). */
int sys_devices_cpu_online_count(void);

/* /sys/class/net children for the pseudo_fs registry readdir. */
int sys_class_net_collect_children(const char *dir_path,
				   struct vfs_dirent *entries, int max_entries,
				   int start_n);
int sys_class_net_path_has_children(const char *path);

/* /sys/firmware/ir0 — boot protocol snapshot (see Documentation/sysfs-firmware-ir0.md). */
int sys_firmware_ir0_protocol_read_reg(char *buf, size_t count);
int sys_firmware_ir0_bootloader_read_reg(char *buf, size_t count);
int sys_firmware_ir0_cmdline_read_reg(char *buf, size_t count);
int sys_firmware_ir0_memory_map_read_reg(char *buf, size_t count);
int sys_firmware_ir0_framebuffer_read_reg(char *buf, size_t count);
int sys_firmware_ir0_acpi_rsdp_read_reg(char *buf, size_t count);
int sys_firmware_ir0_dtb_read_reg(char *buf, size_t count);
int sys_firmware_ir0_boot_detail_read_reg(char *buf, size_t count);

