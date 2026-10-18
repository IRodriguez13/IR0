/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: product_ipc.c
 * Description: PIPE2 / SOCKETPAIR owned by ipc/ staged tables
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/early_rootfs.h>
#include <ir0/product_ipc.h>
#include <ir0/syscall_id.h>

#define ENOSYS 38

static int64_t product_ipc_syscall(void *context, enum ir0_syscall_id id,
				   uint64_t a0, uint64_t a1, uint64_t a2,
				   uint64_t a3, uint64_t a4, uint64_t a5)
{
	(void)context;
	(void)a4;
	(void)a5;
	if (id == IR0_SYSCALL_PIPE2)
		return early_rootfs_pipe2(a0, (int)a1);
	if (id == IR0_SYSCALL_SOCKETPAIR)
		return early_rootfs_socketpair((int)a0, (int)a1, (int)a2, a3);
	return -ENOSYS;
}

static const enum ir0_syscall_id g_product_ipc_ids[] = {
	IR0_SYSCALL_PIPE2,
	IR0_SYSCALL_SOCKETPAIR,
};

const struct syscall_context_provider ir0_product_ipc_provider = {
	.ids = g_product_ipc_ids,
	.count = sizeof(g_product_ipc_ids) / sizeof(g_product_ipc_ids[0]),
	.handler = product_ipc_syscall,
};
