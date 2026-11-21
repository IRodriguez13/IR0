/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_diag.h
 * Description: MM diagnostics facade (mm/diag.c, implemented over KTM).
 *              pf_debug_stack_adjacent is the portable page-fault
 *              hook the ISA backends implement.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/page_fault.h>
#include <ir0/process.h>

/*
 * Nonzero when the named KTM fault-injection point fires; MM allocation
 * paths use it to exercise their failure branches.
 */
int mm_diag_fault_hit(const char *name);

/* @frame is opaque to common MM code and decoded only by the backend. */
void pf_debug_stack_adjacent(uint64_t *frame, uint64_t fault_addr,
			     const struct page_fault_info *info,
			     process_t *p);
