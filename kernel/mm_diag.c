/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: mm_diag.c
 * Description: KTM-backed diagnostics adapter for portable MM.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/mm_diag.h>
#include <ir0/process.h>
#include <ir0/ktm/deferred.h>
#include <ir0/ktm/event.h>
#include <ir0/ktm/fault.h>
#include <ir0/ktm/user_canary.h>
#include <ktm.h>
#include <ktm_probe_diag.h>

void mm_diag_probe_user_fault(struct process *process, uint64_t address,
			      uint64_t instruction_pointer)
{
	ktm_probe_diag_pf(process, address, instruction_pointer);
}

void mm_diag_dump_user_segv(struct process *process, uint64_t stack_top)
{
	if (!process)
		return;
	ktm_user_canary_check(process_pgd(process), stack_top,
			      (uint32_t)process->task.pid, "segv");
	ktm_event_ring_dump(48, (1U << KTM_SUBSYS_IPC) |
			    (1U << KTM_SUBSYS_SCHED) |
			    (1U << KTM_SUBSYS_MM));
}

void mm_diag_record_page_fault(uint32_t pid, uint64_t address,
			       uint64_t flags, uint64_t instruction_pointer)
{
	ktm_deferred_record(KTM_DEFERRED_PAGE_FAULT, pid, address, flags,
			    instruction_pointer);
}

int mm_diag_fault_hit(const char *name)
{
	return KTM_FAULT_HIT(name);
}
