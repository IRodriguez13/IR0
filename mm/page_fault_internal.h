/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page_fault_internal.h
 * Description: Internal facade of the mm/page_fault*.c units (dispatcher,
 *              user faults, COW write faults, kernel faults). Not public.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/page_fault.h>
#include <ir0/process.h>

/* Supervisor 2 MiB identity under each process address-space root. */
#define PF_IDENTITY_USER_FLOOR 0x00600000UL

/* VMA lookups and anonymous page provisioning (page_fault_user.c). */
int pf_addr_in_heap(process_t *p, uint64_t fa);
int pf_addr_in_stack(process_t *p, uint64_t fa);
int pf_addr_may_promote_identity(process_t *p, uint64_t fa);
void pf_demand_zero_page(process_t *current, uint64_t fault_addr,
			 uint64_t map_flags, uint64_t *stack,
			 const struct page_fault_info *info);
int pf_identity_to_anon_zero(process_t *current, uint64_t vaddr_aligned,
			     uint64_t map_flags);

/* Deliver SIGSEGV, or kill the task when no handler takes it (page_fault.c). */
void pf_user_segv(process_t *p, uint64_t *stack, uint64_t fault_addr,
		  const struct page_fault_info *info);

/*
 * Diagnostic hooks (mm/diag.c). They only report; the fault policy above
 * decides. @stack is the opaque ISA frame, decoded only by arch hooks.
 */
struct ir0_fault;
void pf_diag_fault_entry(const struct page_fault_info *info, uint64_t *stack);
void pf_diag_user_segv(process_t *p, uint64_t fault_addr,
		       const struct page_fault_info *info);
void pf_diag_kernel_uaccess(const struct ir0_fault *fault, unsigned int pid,
			    const char *comm);
void pf_diag_kernel_fault(const struct ir0_fault *fault,
			  const struct page_fault_info *info, unsigned int pid);

/* Fault classes, called by mm_page_fault_handle(). */
void pf_user_not_present(const struct page_fault_info *info, uint64_t *stack,
			 uint64_t fault_addr);
int pf_user_read_present(const struct page_fault_info *info, uint64_t *stack,
			 uint64_t fault_addr);
void pf_user_write_present(const struct page_fault_info *info,
			   uint64_t *stack, uint64_t fault_addr);
void pf_kernel_fault(const struct page_fault_info *info, uint64_t fault_addr);
