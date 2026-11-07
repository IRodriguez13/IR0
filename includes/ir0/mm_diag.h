/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_diag.h
 * Description: Optional diagnostics boundary for portable MM paths.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct process;

void mm_diag_probe_user_fault(struct process *process, uint64_t address,
			      uint64_t instruction_pointer);
void mm_diag_dump_user_segv(struct process *process, uint64_t stack_top);
void mm_diag_record_page_fault(uint32_t pid, uint64_t address,
			       uint64_t flags, uint64_t instruction_pointer);
int mm_diag_fault_hit(const char *name);
