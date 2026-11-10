/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sigtramp.c
 * Description: Map the ISA rt_sigreturn trampoline page at exec so handlers
 *              installed without SA_RESTORER can return (Linux arm64
 *              setup_return() falls back to the vDSO sigtramp).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/sigtramp.h>
#include <kernel/process.h>
#include <ir0/paging.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>

static const uint8_t sigtramp_code[] = IR0_ARCH_SIGTRAMP_CODE;

int sigtramp_map(struct process *proc)
{
	address_space_root_t root;
	uintptr_t base = (uintptr_t)sigtramp_addr();

	if (base == 0)
		return -ENOSYS;
	if (!proc || proc->mode != USER_MODE)
		return -EINVAL;
	root = process_pgd(proc);
	if (!root)
		return -EINVAL;

	/* Populate through RW/NX, then expose the final RX page. */
	if (map_user_region_in_directory(root, base, PAGE_SIZE_4KB,
					 PAGE_USER | PAGE_RW) != 0)
		return -ENOMEM;
	if (copy_to_user_mm(root, base, sigtramp_code,
			    sizeof(sigtramp_code)) != 0)
		return -EFAULT;
	if (protect_user_region_in_directory(root, base, PAGE_SIZE_4KB,
					     PAGE_USER | PAGE_EXEC) != 0)
		return -EFAULT;
	return 0;
}
