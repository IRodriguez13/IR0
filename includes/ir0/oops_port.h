/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: oops_port.h
 * Description: ISA extra panic diagnostics — weak noop in arch/common.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

struct ir0_fault;
struct process;

void ir0_oops_dump_arch_extra(void);

/* Print @fault's ISA-specific registers under the portable FAULT FRAME. */
void ir0_oops_show_fault_regs(const struct ir0_fault *fault);

/*
 * Log ISA-specific evidence for a user fault (instruction bytes, return
 * address) and return @classify, refined when that evidence names the
 * failure more precisely. @proc may be NULL.
 */
const char *ir0_oops_user_fault_extra(const struct ir0_fault *fault,
				      struct process *proc,
				      const char *classify);

#ifdef __cplusplus
}
#endif
