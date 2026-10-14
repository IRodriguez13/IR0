/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_pf_debug.h
 * Description: Optional ISA page-fault forensic hooks.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/page_fault.h>
#include <ir0/process.h>

/* @arch_frame is opaque to common MM code and decoded only by the backend. */
void pf_debug_stack_adjacent(uint64_t *arch_frame, uint64_t fault_addr,
			     const struct page_fault_info *info,
			     process_t *p);

void pf_debug_memmove_fault(uint64_t *arch_frame, uint64_t fault_addr,
			    const struct page_fault_info *info,
			    process_t *p);
