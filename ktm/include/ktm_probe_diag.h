/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_probe_diag.h
 * Description: D1.3 musl/BusyBox probe-path diagnostics (gated, temporary)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>
#include <stdint.h>

struct process;

#if defined(CONFIG_KTM_PROBE_DIAG) && CONFIG_KTM_PROBE_DIAG

void ktm_probe_diag_note_comm(struct process *p);
void ktm_probe_diag_execve(struct process *p, const char *path);
void ktm_probe_diag_syscall_pre(uint64_t nr, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5,
				uint64_t a6, uint64_t rip);
void ktm_probe_diag_syscall_post(uint64_t nr, int64_t ret);
void ktm_probe_diag_pf(struct process *p, uint64_t fault_addr,
		       uint64_t fault_rip);

#else

static inline void ktm_probe_diag_note_comm(struct process *p)
{
	(void)p;
}

static inline void ktm_probe_diag_execve(struct process *p, const char *path)
{
	(void)p;
	(void)path;
}

static inline void ktm_probe_diag_syscall_pre(uint64_t nr, uint64_t a1,
					    uint64_t a2, uint64_t a3,
					    uint64_t a4, uint64_t a5,
					    uint64_t a6, uint64_t rip)
{
	(void)nr;
	(void)a1;
	(void)a2;
	(void)a3;
	(void)a4;
	(void)a5;
	(void)a6;
	(void)rip;
}

static inline void ktm_probe_diag_syscall_post(uint64_t nr, int64_t ret)
{
	(void)nr;
	(void)ret;
}

static inline void ktm_probe_diag_pf(struct process *p, uint64_t fault_addr,
				     uint64_t fault_rip)
{
	(void)p;
	(void)fault_addr;
	(void)fault_rip;
}

#endif /* CONFIG_KTM_PROBE_DIAG */
