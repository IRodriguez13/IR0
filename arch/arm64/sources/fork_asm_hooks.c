/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fork_asm_hooks.c
 * Description: ARM64 no-op stubs for portable fork_ret_* declarations.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

void fork_ret_emit_pre_return(void)
{
}

void fork_restore_emit_pre_iretq(void)
{
}

void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw)
{
	(void)rax_hw;
	(void)rip_hw;
	(void)rsp_hw;
}

int fork_flow_note_debug_exception(uint64_t *stack)
{
	(void)stack;
	return 0;
}

void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall)
{
	(void)rip_hw;
	(void)nr;
	(void)from_syscall;
}
