/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: sigcontext_x86_64.h
 * Description: Linux/x86-64 sigcontext uapi layout (rt_sigreturn / musl).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

/*
 * Portable code must not open these fields — use arch_task_load/store_sigcontext
 * and arch_signal_* helpers only.
 */
struct sigcontext
{
	uint64_t r15;
	uint64_t r14;
	uint64_t r13;
	uint64_t r12;
	uint64_t rbp;
	uint64_t rbx;
	uint64_t r11;
	uint64_t r10;
	uint64_t r9;
	uint64_t r8;
	uint64_t rax;
	uint64_t rcx;
	uint64_t rdx;
	uint64_t rsi;
	uint64_t rdi;
	uint64_t orig_rax;
	uint64_t rip;
	uint64_t cs;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t ss;
};
