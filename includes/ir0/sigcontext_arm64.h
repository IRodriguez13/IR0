/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sigcontext_arm64.h
 * Description: Linux aarch64 sigcontext uapi layout (rt_sigreturn / musl).
 *
 * Source: arch/arm64/include/uapi/asm/sigcontext.h (Linux). Portable code must
 * not open these fields — use arch_task_load/store_sigcontext and arch_signal_*.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Signal context structure — state before the handler was invoked.
 * __reserved holds FPSIMD / SVE / ESR records (_aarch64_ctx chain); IR0 zeros
 * it on store until fpsimd save is wired.
 */
struct sigcontext
{
	uint64_t fault_address;
	uint64_t regs[31];
	uint64_t sp;
	uint64_t pc;
	uint64_t pstate;
	uint8_t __reserved[4096] __attribute__((__aligned__(16)));
};

/* Header for records placed in sigcontext.__reserved (Linux uapi). */
struct _aarch64_ctx
{
	uint32_t magic;
	uint32_t size;
};

#define FPSIMD_MAGIC 0x46508001U

struct fpsimd_context
{
	struct _aarch64_ctx head;
	uint32_t fpsr;
	uint32_t fpcr;
	__uint128_t vregs[32];
};
