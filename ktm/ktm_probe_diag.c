/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_probe_diag.c
 * Description: D1.3 musl probe-path syscall timeline + PF VMA/PTE dump
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_probe_diag.h>

#if defined(CONFIG_KTM_PROBE_DIAG) && CONFIG_KTM_PROBE_DIAG

#include <ktm.h>
#include <ir0/bits/syscall_linux.h>
#include <ir0/process.h>
#include <ir0/paging.h>
#include <ir0/serial_io.h>
#include <ir0/signals.h>
#include <config.h>
#include <string.h>

#define PROBE_SC_RING_CAP 128
#define PROBE_MUSL_ARENA   0x08000000UL
#define PROBE_PF_FOCUS     0x08004000UL

#define MAP_ANONYMOUS 0x20
#define MAP_FIXED     0x10
#define MAP_PRIVATE   0x02
#define PROT_READ     0x1
#define PROT_WRITE    0x2
#define PROT_EXEC     0x4
#define PROT_NONE     0x0

struct probe_sc_entry
{
	uint32_t seq;
	uint32_t nr;
	int64_t  ret;
	uint64_t a0;
	uint64_t a1;
	uint64_t a2;
	uint64_t a3;
	uint64_t rip;
};

static uint32_t probe_target_pid;
static uint32_t probe_sc_seq;
static uint32_t probe_sc_head;
static struct probe_sc_entry probe_sc_ring[PROBE_SC_RING_CAP];

static int probe_comm_is_sh(const struct process *p)
{
	if (!p || !p->comm[0])
		return 0;
	return (p->comm[0] == 's' && p->comm[1] == 'h' &&
		p->comm[2] == '\0');
}

static int probe_path_is_sh(const char *path)
{
	size_t n;

	if (!path)
		return 0;
	n = strlen(path);
	if (n >= 3 && strcmp(path + n - 3, "/sh") == 0)
		return 1;
	return (strcmp(path, "/bin/sh") == 0 || strcmp(path, "sh") == 0);
}

static void probe_arm_target(struct process *p)
{
	if (!p)
		return;
	probe_target_pid = (uint32_t)p->task.pid;
	serial_print("[PROBE][ARM] pid=");
	serial_print_hex32(probe_target_pid);
	serial_print(" comm=");
	serial_print(p->comm[0] ? p->comm : "(none)");
	serial_print("\n");
}

void ktm_probe_diag_note_comm(struct process *p)
{
	if (!p)
		return;
	if (probe_comm_is_sh(p))
		probe_arm_target(p);
}

void ktm_probe_diag_execve(struct process *p, const char *path)
{
	if (!p || !path)
		return;

	serial_print("[PROBE][EXEC] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" path=");
	serial_print(path);
	serial_print("\n");

	if (probe_path_is_sh(path) || probe_comm_is_sh(p))
		probe_arm_target(p);
}

static int probe_is_target(void)
{
	return (current_process &&
		(uint32_t)current_process->task.pid == probe_target_pid);
}

static const char *probe_sc_name(uint64_t nr)
{
	switch (nr)
	{
	case __NR_mmap:           return "mmap";
	case __NR_mprotect:      return "mprotect";
	case __NR_munmap:        return "munmap";
	case __NR_brk:           return "brk";
	case __NR_rt_sigaction:  return "rt_sigaction";
	case __NR_rt_sigprocmask: return "rt_sigprocmask";
	case __NR_mincore:       return "mincore";
	case __NR_fork:          return "fork";
	case __NR_clone:         return "clone";
	case __NR_vfork:         return "vfork";
	case __NR_execve:        return "execve";
	default:                 return "?";
	}
}

static int probe_sc_interesting(uint64_t nr)
{
	switch (nr)
	{
	case __NR_mmap:
	case __NR_mprotect:
	case __NR_munmap:
	case __NR_brk:
	case __NR_rt_sigaction:
	case __NR_rt_sigprocmask:
	case __NR_mincore:
	case __NR_fork:
	case __NR_clone:
	case __NR_vfork:
	case __NR_execve:
		return 1;
	default:
		return 0;
	}
}

static void probe_print_prot(int prot)
{
	serial_print((prot & PROT_READ) ? "r" : "-");
	serial_print((prot & PROT_WRITE) ? "w" : "-");
	serial_print((prot & PROT_EXEC) ? "x" : "-");
	if (prot == PROT_NONE)
		serial_print("(none)");
}

static void probe_print_mmap_flags(int flags)
{
	if (flags & MAP_FIXED)
		serial_print("FIXED|");
	if (flags & MAP_ANONYMOUS)
		serial_print("ANON|");
	if (flags & MAP_PRIVATE)
		serial_print("PRIV|");
}

static void probe_sc_ring_push(uint64_t nr, uint64_t a0, uint64_t a1,
			       uint64_t a2, uint64_t a3, uint64_t rip)
{
	struct probe_sc_entry *e;
	uint32_t idx;

	idx = probe_sc_head % PROBE_SC_RING_CAP;
	e = &probe_sc_ring[idx];
	probe_sc_head++;
	probe_sc_seq++;

	e->seq = probe_sc_seq;
	e->nr = (uint32_t)nr;
	e->ret = INT64_MIN;
	e->a0 = a0;
	e->a1 = a1;
	e->a2 = a2;
	e->a3 = a3;
	e->rip = rip;
}

static void probe_sc_ring_set_ret(uint64_t nr, int64_t ret)
{
	uint32_t idx;
	struct probe_sc_entry *e;

	if (probe_sc_head == 0)
		return;
	idx = (probe_sc_head - 1) % PROBE_SC_RING_CAP;
	e = &probe_sc_ring[idx];
	if (e->nr != (uint32_t)nr)
		return;
	e->ret = ret;
}

static void probe_log_sigaction_pre(uint64_t a1, uint64_t a2, uint64_t a4)
{
	serial_print(" signum=");
	serial_print_hex64(a1);
	if (a2)
	{
		serial_print(" act_ptr=");
		serial_print_hex64(a2);
	}
	serial_print(" sigsetsize=");
	serial_print_hex64(a4);
}

static void probe_log_sigprocmask_pre(uint64_t a1, uint64_t a2)
{
	serial_print(" how=");
	serial_print_hex64(a1);
	serial_print(" set_ptr=");
	serial_print_hex64(a2);
}

static void probe_log_mmap_pre(uint64_t a1, uint64_t a2, uint64_t a3,
			       uint64_t a4, uint64_t a5, uint64_t a6)
{
	serial_print(" addr=");
	serial_print_hex64(a1);
	serial_print(" len=");
	serial_print_hex64(a2);
	serial_print(" prot=");
	probe_print_prot((int)a3);
	serial_print(" flags=");
	probe_print_mmap_flags((int)a4);
	serial_print(" fd=");
	serial_print_hex64(a5);
	serial_print(" off=");
	serial_print_hex64(a6);
}

static void probe_log_mprotect_pre(uint64_t a1, uint64_t a2, uint64_t a3)
{
	serial_print(" addr=");
	serial_print_hex64(a1);
	serial_print(" len=");
	serial_print_hex64(a2);
	serial_print(" prot=");
	probe_print_prot((int)a3);
}

void ktm_probe_diag_syscall_pre(uint64_t nr, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5,
				uint64_t a6, uint64_t rip)
{
	if (!probe_is_target())
		return;

	probe_sc_ring_push(nr, a1, a2, a3, a4, rip);

	if (!probe_sc_interesting(nr))
		return;

	serial_print("[PROBE][SC] pre pid=");
	serial_print_hex32(probe_target_pid);
	serial_print(" nr=");
	serial_print_hex64(nr);
	serial_print(" ");
	serial_print(probe_sc_name(nr));
	serial_print(" rip=");
	serial_print_hex64(rip);

	switch (nr)
	{
	case __NR_rt_sigaction:
		probe_log_sigaction_pre(a1, a2, a4);
		break;
	case __NR_rt_sigprocmask:
		probe_log_sigprocmask_pre(a1, a2);
		break;
	case __NR_mmap:
		probe_log_mmap_pre(a1, a2, a3, a4, a5, a6);
		break;
	case __NR_mprotect:
		probe_log_mprotect_pre(a1, a2, a3);
		break;
	case __NR_munmap:
		serial_print(" addr=");
		serial_print_hex64(a1);
		serial_print(" len=");
		serial_print_hex64(a2);
		break;
	case __NR_brk:
		serial_print(" brk=");
		serial_print_hex64(a1);
		break;
	case __NR_mincore:
		serial_print(" addr=");
		serial_print_hex64(a1);
		serial_print(" len=");
		serial_print_hex64(a2);
		serial_print(" vec_ptr=");
		serial_print_hex64(a3);
		break;
	case __NR_execve:
		serial_print(" path_ptr=");
		serial_print_hex64(a1);
		break;
	default:
		break;
	}

	serial_print("\n");
}

static struct mmap_region *probe_vma_containing(struct process *p, uint64_t fa)
{
	struct mmap_region *r;

	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uintptr_t base = (uintptr_t)r->addr;
		uint64_t end = base + (uint64_t)r->length;

		if (fa >= (uint64_t)base && fa < end)
			return r;
	}
	return NULL;
}

static void probe_dump_pte(struct process *p, uint64_t va, const char *tag)
{
	uint64_t pte_flags = 0;
	int mapped;
	uint64_t *pte;

	if (!p || !p->page_directory)
		return;

	mapped = is_page_mapped_in_directory(p->page_directory, va, &pte_flags);
	pte = paging_get_pte(p->page_directory, (uintptr_t)(va & ~0xFFFULL));

	serial_print("[PROBE][PTE] ");
	serial_print(tag ? tag : "?");
	serial_print(" va=");
	serial_print_hex64(va);
	serial_print(" mapped=");
	serial_print_hex32(mapped > 0 ? 1U : 0U);
	serial_print(" present=");
	serial_print_hex32((mapped > 0 && pte && (*pte & PAGE_PRESENT)) ? 1U : 0U);
	serial_print(" user=");
	serial_print_hex32(pte_flags & PAGE_USER ? 1U : 0U);
	serial_print(" rw=");
	serial_print_hex32(pte_flags & PAGE_RW ? 1U : 0U);
	serial_print(" nx=");
	serial_print_hex32(pte && (*pte & PAGE_NX) ? 1U : 0U);
	serial_print("\n");
}

static void probe_dump_vma_region(const char *kind, uint64_t start, uint64_t end,
				  int prot, int flags, const char *rel)
{
	serial_print("[PROBE][VMA] ");
	serial_print(rel ? rel : "?");
	serial_print(" ");
	serial_print(kind);
	serial_print(" [");
	serial_print_hex64(start);
	serial_print(",");
	serial_print_hex64(end);
	serial_print(") prot=");
	probe_print_prot(prot);
	if (flags >= 0)
	{
		serial_print(" flags=");
		probe_print_mmap_flags(flags);
	}
	serial_print("\n");
}

static void probe_dump_all_vmas(struct process *p)
{
	struct mmap_region *r;
	uint64_t hs;
	uint64_t he;
	uint64_t ss;
	uint64_t se;

	if (!p)
		return;

	hs = p->heap_start;
	he = p->heap_end;
	ss = p->stack_start;
	se = p->stack_start + p->stack_size;

	probe_dump_vma_region("heap", hs, he, PROT_READ | PROT_WRITE, -1, "all");
	probe_dump_vma_region("stack", ss, se, PROT_READ | PROT_WRITE, -1, "all");

	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;

		probe_dump_vma_region("mmap", start, end, r->prot, r->flags, "all");
	}
}

static void probe_dump_vma_window(struct process *p, uint64_t fa)
{
	struct mmap_region *r;
	struct mmap_region *prev_mmap;
	struct mmap_region *next_mmap;
	uint64_t prev_end;
	uint64_t next_start;
	int found_mmap;

	prev_mmap = NULL;
	next_mmap = NULL;
	prev_end = 0;
	next_start = ~0ULL;
	found_mmap = 0;

	if (!p)
		return;

	serial_print("[PROBE][VMA] focus_cr2=");
	serial_print_hex64(fa);
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" comm=");
	serial_print(p->comm[0] ? p->comm : "(none)");
	serial_print("\n");

	if (fa >= p->heap_start && fa < p->heap_end)
		serial_print("[PROBE][VMA] IN_HEAP=1\n");
	else if (fa >= p->stack_start && fa < p->stack_start + p->stack_size)
		serial_print("[PROBE][VMA] IN_STACK=1\n");
	else
		serial_print("[PROBE][VMA] IN_HEAP=0 IN_STACK=0\n");

	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;

		if (end <= fa && end > prev_end)
		{
			prev_end = end;
			prev_mmap = r;
		}
		if (start > fa && start < next_start)
		{
			next_start = start;
			next_mmap = r;
		}
		if (fa >= start && fa < end)
		{
			found_mmap = 1;
			probe_dump_vma_region("mmap", start, end, r->prot, r->flags,
					      "HIT");
		}
	}

	if (!found_mmap)
		serial_print("[PROBE][VMA] HIT none (cr2 outside mmap VMA list)\n");

	if (prev_mmap)
	{
		uint64_t s = (uint64_t)(uintptr_t)prev_mmap->addr;
		uint64_t e = s + (uint64_t)prev_mmap->length;

		probe_dump_vma_region("mmap", s, e, prev_mmap->prot,
				      prev_mmap->flags, "PREV");
	}
	else
		serial_print("[PROBE][VMA] PREV none\n");

	if (next_mmap)
	{
		uint64_t s = (uint64_t)(uintptr_t)next_mmap->addr;
		uint64_t e = s + (uint64_t)next_mmap->length;

		probe_dump_vma_region("mmap", s, e, next_mmap->prot,
				      next_mmap->flags, "NEXT");
	}
	else
		serial_print("[PROBE][VMA] NEXT none\n");

	if (fa >= PROBE_MUSL_ARENA && fa < PROBE_MUSL_ARENA + 0x200000UL)
		serial_print("[PROBE][VMA] in_musl_arena=1\n");
}

static void probe_dump_sc_ring(void)
{
	uint32_t count;
	uint32_t i;

	if (probe_sc_head == 0)
	{
		serial_print("[PROBE][SC_RING] empty\n");
		return;
	}

	count = probe_sc_head;
	if (count > PROBE_SC_RING_CAP)
		count = PROBE_SC_RING_CAP;

	serial_print("[PROBE][SC_RING] last ");
	serial_print_hex32(count);
	serial_print(" syscalls pid=");
	serial_print_hex32(probe_target_pid);
	serial_print("\n");

	for (i = 0; i < count; i++)
	{
		uint32_t idx = (probe_sc_head - count + i) % PROBE_SC_RING_CAP;
		const struct probe_sc_entry *e = &probe_sc_ring[idx];

		serial_print("  #");
		serial_print_hex32(e->seq);
		serial_print(" ");
		serial_print(probe_sc_name(e->nr));
		serial_print("(");
		serial_print_hex64(e->nr);
		serial_print(") rip=");
		serial_print_hex64(e->rip);
		serial_print(" a0=");
		serial_print_hex64(e->a0);
		serial_print(" a1=");
		serial_print_hex64(e->a1);
		serial_print(" a2=");
		serial_print_hex64(e->a2);
		serial_print(" ret=");
		serial_print_hex64((uint64_t)e->ret);
		serial_print("\n");
	}
}

void ktm_probe_diag_syscall_post(uint64_t nr, int64_t ret)
{
	struct mmap_region *hit;
	uint64_t a0;
	uint64_t a1;

	if (!probe_is_target())
		return;

	probe_sc_ring_set_ret(nr, ret);

	if (!probe_sc_interesting(nr))
		return;

	serial_print("[PROBE][SC] post pid=");
	serial_print_hex32(probe_target_pid);
	serial_print(" nr=");
	serial_print_hex64(nr);
	serial_print(" ");
	serial_print(probe_sc_name(nr));
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);

	if (ret < 0 && ret > -4096)
	{
		serial_print(" errno=");
		serial_print_hex64((uint64_t)(-ret));
	}

	if (nr == __NR_mincore && ret < 0)
		serial_print(" (mincore_unimplemented_or_fail)");

	switch (nr)
	{
	case __NR_rt_sigaction:
		if (probe_sc_head > 0 && current_process)
		{
			const struct probe_sc_entry *e =
				&probe_sc_ring[(probe_sc_head - 1) %
					       PROBE_SC_RING_CAP];
			int signum = (int)e->a0;

			if (signum > 0 && signum < _NSIG)
			{
				serial_print(" handler_after=");
				serial_print_hex64((uint64_t)(uintptr_t)
					current_process->signal_handlers[signum]);
				serial_print(" sa_flags=");
				serial_print_hex32(
					current_process->signal_sa_flags[signum]);
			}
		}
		break;
	case __NR_mmap:
		if (ret >= 0)
		{
			serial_print(" map_at=");
			serial_print_hex64((uint64_t)ret);
			hit = probe_vma_containing(current_process, (uint64_t)ret);
			if (hit)
			{
				serial_print(" vma_ok=1 prot=");
				probe_print_prot(hit->prot);
			}
			else
				serial_print(" vma_ok=0");
		}
		break;
	case __NR_mprotect:
		if (probe_sc_head > 0)
		{
			const struct probe_sc_entry *e =
				&probe_sc_ring[(probe_sc_head - 1) %
					       PROBE_SC_RING_CAP];

			a0 = e->a0;
			a1 = e->a1;
			if (ret == 0)
			{
				probe_dump_pte(current_process, a0, "mprotect_post");
				hit = probe_vma_containing(current_process, a0);
				if (hit)
				{
					serial_print(" vma_prot=");
					probe_print_prot(hit->prot);
				}
			}
			(void)a1;
		}
		break;
	case __NR_brk:
		if (current_process)
		{
			serial_print(" heap=[");
			serial_print_hex64(current_process->heap_start);
			serial_print(",");
			serial_print_hex64(current_process->heap_end);
			serial_print(")");
		}
		break;
	default:
		break;
	}

	serial_print("\n");
}

void ktm_probe_diag_pf(struct process *p, uint64_t fault_addr,
		       uint64_t fault_rip)
{
	if (!p)
		return;

	if (!probe_comm_is_sh(p) &&
	    (uint32_t)p->task.pid != probe_target_pid)
		return;

	if (fault_addr != PROBE_PF_FOCUS &&
	    (fault_addr < PROBE_MUSL_ARENA ||
	     fault_addr >= PROBE_MUSL_ARENA + 0x200000UL))
		return;

	serial_print("\n=== [PROBE][PF_DUMP] D1.3 musl probe diagnostics ===\n");
	serial_print("[PROBE][PF] cr2=");
	serial_print_hex64(fault_addr);
	serial_print(" rip=");
	serial_print_hex64(fault_rip);
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" handler_segv=");
	serial_print_hex64((uint64_t)(uintptr_t)p->signal_handlers[SIGSEGV]);
	serial_print(" proc_mask=");
	serial_print_hex32(p->signal_mask);
	serial_print(" ignored=");
	serial_print_hex32(p->signal_ignored);
	serial_print("\n");

	probe_dump_vma_window(p, fault_addr);
	probe_dump_all_vmas(p);
	probe_dump_pte(p, fault_addr, "cr2");
	probe_dump_pte(p, fault_addr & ~0xFFFULL, "cr2_page");
	if (fault_addr >= 0x1000UL)
		probe_dump_pte(p, fault_addr - 0x1000UL, "page_before");
	probe_dump_pte(p, (fault_addr & ~0xFFFULL) + 0x1000UL, "page_after");

	serial_print("[PROBE][ANSWERS]\n");
	serial_print("  sigsegv_handler_registered=");
	serial_print(signals_has_user_handler(p, SIGSEGV) ? "1" : "0");
	serial_print("\n");

	probe_dump_sc_ring();
	ktm_flight_dump_last(64);
	serial_print("=== [PROBE][PF_DUMP] end ===\n\n");
}

#endif /* CONFIG_KTM_PROBE_DIAG */
