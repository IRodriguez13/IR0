/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: sysfs_internal.h
 * Description: /sys file readers and writers that fs/sysfs.c implements
 *              and fs/pseudo_fs_nodes.c binds to their paths. Not a public
 *              ABI; only fs/ (and the host-test stubs) include this header.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/sysfs.h>

/* Attr ids for /sys/class/net/<iface>/… (0 = the iface directory itself). */
enum
{
	SYS_NET_ATTR_DIR = 0,
	SYS_NET_ATTR_NAME = 1,
	SYS_NET_ATTR_ADDRESS = 2,
	SYS_NET_ATTR_MTU = 3,
	SYS_NET_ATTR_STATE = 4,
	SYS_NET_ATTR_RX_PACKETS = 5,
	SYS_NET_ATTR_TX_PACKETS = 6,
	SYS_NET_ATTR_RX_BYTES = 7,
	SYS_NET_ATTR_TX_BYTES = 8
};

int sys_kernel_hostname_write_reg(const char *buf, size_t count);
int sys_kernel_version_read_reg(char *buf, size_t count);
int sys_kernel_osrelease_read_reg(char *buf, size_t count);
int sys_kernel_max_processes_read_reg(char *buf, size_t count);
int sys_kernel_max_processes_write_reg(const char *buf, size_t count);
uint32_t sys_kernel_max_processes_limit(void);
/* /sys/kernel/panic — read: help text; write: force a kernel panic (test). */
int sys_kernel_panic_read_reg(char *buf, size_t count);
int sys_kernel_panic_write_reg(const char *buf, size_t count);
int sys_console_mode_read_reg(char *buf, size_t count);
int sys_devices_system_read_reg(char *buf, size_t count);
int sys_devices_cpu_read_reg(char *buf, size_t count, unsigned cpu);
int sys_devices_cpu_online_read_reg(char *buf, size_t count, unsigned cpu);
int sys_devices_cpu_online_write_reg(unsigned cpu, const char *buf, size_t count);
int sys_devices_block_read_reg(char *buf, size_t count);
int sys_kernel_build_read_reg(char *buf, size_t count);
int sys_kernel_features_read_reg(char *buf, size_t count);
int sys_kernel_mm_read_reg(char *buf, size_t count);
int sys_class_net_list_read_reg(char *buf, size_t count);
int sys_class_net_attr_read_named(char *buf, size_t count, const char *ifname,
				  unsigned attr);
int sys_class_net_parse_attr(const char *attr_name);
int sys_class_net_find_name(const char *ifname);
