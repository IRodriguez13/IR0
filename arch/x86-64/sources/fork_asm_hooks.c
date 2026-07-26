/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fork_asm_hooks.c
 * Description: Minimal symbols for switch_x64.asm fork-return hooks (no diag).
 *
 * Historical FORK_RET / FORK_RESTORE audits lived in portable fork.c; they are
 * retired. ASM still references these symbols — keep zeroed BSS + no-op C
 * entry points so the switch path stays linked without ISA debt in fork.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>

/* Matches former fork_ret_pre_regs_t layout (11 qwords). */
uint64_t fork_ret_pre_regs[11];

/*
 * Opaque blob sized for former fork_restore_audit_t (~300+ bytes).
 * ASM writes relative offsets; content is ignored (no classify/log).
 */
uint64_t fork_restore_audit[48];

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
