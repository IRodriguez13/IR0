/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: class_b.h
 * Description: Host/KTM fixtures for Class B (KERNEL_CS + userspace RIP)
 *
 * Linux-like contract: syscall_frame holds user pt_regs; task.rip must not
 * receive user RIP via sync at syscall entry. Class B is the illegal pairing
 * KERNEL_CS + user RIP on task_t (kernel_ret would jmp to user VA).
 *
 * Mirrors switch_to KTM fault "sched.class_b_arm_window" and
 * process_ctx_invariant.h. Include via -Itests/mocks → <sched/class_b.h>
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/process_ctx_invariant.h>

/* Match config.h without pulling the full kernel config into host tests. */
#define IR0_MOCK_KERNEL_CODE_SEL 0x08u
#define IR0_MOCK_KERNEL_DATA_SEL 0x10u
#define IR0_MOCK_USER_CODE_SEL   0x1Bu
#define IR0_MOCK_USER_DATA_SEL   0x23u

/* Synthetic user RIP used by CLASS_B_FAULT_INJECT (switch_to). */
#define IR0_MOCK_CLASS_B_USER_RIP (IR0_USER_RIP_LO + 0x1000ULL)

/* Plausible user RSP when seeding syscall_frame for REPAIR after inject. */
#define IR0_MOCK_CLASS_B_USER_RSP 0x00007FFFFFF0ULL

/* Sample kernel .text RIP (post-save / wait kernel_ret — not Class B). */
#define IR0_MOCK_KERNEL_TEXT_RIP 0x00101000ULL

typedef struct ir0_mock_cs_rip
{
	uint64_t cs;
	uint64_t rip;
} ir0_mock_cs_rip_t;

/*
 * A: user CS + user RIP — OK for iretq (pt_regs applied to task at exit-to-user).
 * Historical name "sync_like" kept; entry sync into task.rip is a no-op now.
 */
static inline ir0_mock_cs_rip_t ir0_mock_class_b_user_iretq_ok(void)
{
	ir0_mock_cs_rip_t s = { IR0_MOCK_USER_CODE_SEL, 0x00401234ULL };
	return s;
}

/* Alias for older call sites. */
static inline ir0_mock_cs_rip_t ir0_mock_class_b_sync_like(void)
{
	return ir0_mock_class_b_user_iretq_ok();
}

/* B: illegal Class B pairing. */
static inline ir0_mock_cs_rip_t ir0_mock_class_b_bad(void)
{
	ir0_mock_cs_rip_t s = { IR0_MOCK_KERNEL_CODE_SEL, IR0_MOCK_CLASS_B_USER_RIP };
	return s;
}

/* C: post-save — kernel CS + kernel .text RIP → OK for kernel_ret. */
static inline ir0_mock_cs_rip_t ir0_mock_class_b_post_save(void)
{
	ir0_mock_cs_rip_t s = { IR0_MOCK_KERNEL_CODE_SEL, IR0_MOCK_KERNEL_TEXT_RIP };
	return s;
}

/*
 * Linux-like: pt_regs live in syscall_frame. Soft sync into task is allowed
 * only while USER CS and !want_kernel_ret; KERNEL_CS + user rip is never OK.
 */
static inline int ir0_mock_linux_pt_regs_contract_ok(uint64_t task_cs,
						     uint64_t task_rip,
						     uint8_t want_kernel_ret)
{
	if (want_kernel_ret && (task_cs & 3u) == 0u &&
	    process_rip_in_user_range(task_rip))
		return 0;
	return !process_cs_rip_kernel_ret_bad(task_cs, task_rip);
}

static inline int ir0_mock_class_b_is_bad(const ir0_mock_cs_rip_t *s)
{
	if (!s)
		return 0;
	return process_cs_rip_kernel_ret_bad(s->cs, s->rip);
}
