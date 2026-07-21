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
#include <ir0/ktm/klog.h>
#include <ir0/signals.h>
#include <config.h>
#include <stdarg.h>
#include <string.h>

#define PROBE_SC_LINE_SZ 640

static size_t probe_line_append(char *line, size_t cap, size_t off,
				const char *fmt, ...)
{
	va_list ap;
	int n;

	if (off >= cap)
		return off;
	va_start(ap, fmt);
	n = vsnprintf(line + off, cap - off, fmt, ap);
	va_end(ap);
	if (n > 0)
		return off + (size_t)n;
	return off;
}

static void probe_format_prot(int prot, char *buf, size_t sz)
{
	if (prot == PROT_NONE)
		snprintf(buf, sz, "(none)");
	else
		snprintf(buf, sz, "%c%c%c",
			 (prot & PROT_READ) ? 'r' : '-',
			 (prot & PROT_WRITE) ? 'w' : '-',
			 (prot & PROT_EXEC) ? 'x' : '-');
}

static void probe_format_mmap_flags(int flags, char *buf, size_t sz)
{
	size_t off = 0;

	buf[0] = '\0';
	if (flags & MAP_FIXED)
		off += (size_t)snprintf(buf + off, sz - off, "FIXED|");
	if (flags & MAP_ANONYMOUS)
		off += (size_t)snprintf(buf + off, sz - off, "ANON|");
	if (flags & MAP_PRIVATE)
		off += (size_t)snprintf(buf + off, sz - off, "PRIV|");
}

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
	klog_debug_fmt("PROBE", "[PROBE][ARM] pid=%x comm=%s",
		       (unsigned)probe_target_pid,
		       p->comm[0] ? p->comm : "(none)");
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

	klog_debug_fmt("PROBE", "[PROBE][EXEC] pid=%x path=%s",
		       (unsigned)(uint32_t)p->task.pid, path);

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

static void probe_log_sigaction_pre(char *line, size_t cap, size_t *off,
				    uint64_t a1, uint64_t a2, uint64_t a4)
{
	*off = probe_line_append(line, cap, *off, " signum=%llx",
				 (unsigned long long)a1);
	if (a2)
		*off = probe_line_append(line, cap, *off, " act_ptr=%llx",
					 (unsigned long long)a2);
	*off = probe_line_append(line, cap, *off, " sigsetsize=%llx",
				 (unsigned long long)a4);
}

static void probe_log_sigprocmask_pre(char *line, size_t cap, size_t *off,
				      uint64_t a1, uint64_t a2)
{
	*off = probe_line_append(line, cap, *off, " how=%llx set_ptr=%llx",
				 (unsigned long long)a1,
				 (unsigned long long)a2);
}

static void probe_log_mmap_pre(char *line, size_t cap, size_t *off,
			       uint64_t a1, uint64_t a2, uint64_t a3,
			       uint64_t a4, uint64_t a5, uint64_t a6)
{
	char prot[8];
	char flg[32];

	probe_format_prot((int)a3, prot, sizeof(prot));
	probe_format_mmap_flags((int)a4, flg, sizeof(flg));
	*off = probe_line_append(line, cap, *off,
				 " addr=%llx len=%llx prot=%s flags=%s fd=%llx off=%llx",
				 (unsigned long long)a1, (unsigned long long)a2,
				 prot, flg, (unsigned long long)a5,
				 (unsigned long long)a6);
}

static void probe_log_mprotect_pre(char *line, size_t cap, size_t *off,
				   uint64_t a1, uint64_t a2, uint64_t a3)
{
	char prot[8];

	probe_format_prot((int)a3, prot, sizeof(prot));
	*off = probe_line_append(line, cap, *off,
				 " addr=%llx len=%llx prot=%s",
				 (unsigned long long)a1, (unsigned long long)a2,
				 prot);
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

void ktm_probe_diag_syscall_pre(uint64_t nr, uint64_t a1, uint64_t a2,
				uint64_t a3, uint64_t a4, uint64_t a5,
				uint64_t a6, uint64_t rip)
{
	if (!probe_is_target())
		return;

	probe_sc_ring_push(nr, a1, a2, a3, a4, rip);

	if (!probe_sc_interesting(nr))
		return;

	{
		char line[PROBE_SC_LINE_SZ];
		size_t off = 0;

		off = probe_line_append(
			line, sizeof(line), off,
			"[PROBE][SC] pre pid=%x nr=%llx %s rip=%llx",
			(unsigned)probe_target_pid, (unsigned long long)nr,
			probe_sc_name(nr), (unsigned long long)rip);

		switch (nr)
		{
		case __NR_rt_sigaction:
			probe_log_sigaction_pre(line, sizeof(line), &off, a1, a2,
						a4);
			break;
		case __NR_rt_sigprocmask:
			probe_log_sigprocmask_pre(line, sizeof(line), &off, a1,
						  a2);
			break;
		case __NR_mmap:
			probe_log_mmap_pre(line, sizeof(line), &off, a1, a2, a3,
					   a4, a5, a6);
			break;
		case __NR_mprotect:
			probe_log_mprotect_pre(line, sizeof(line), &off, a1, a2,
					       a3);
			break;
		case __NR_munmap:
			off = probe_line_append(line, sizeof(line), off,
						" addr=%llx len=%llx",
						(unsigned long long)a1,
						(unsigned long long)a2);
			break;
		case __NR_brk:
			off = probe_line_append(line, sizeof(line), off,
						" brk=%llx",
						(unsigned long long)a1);
			break;
		case __NR_mincore:
			off = probe_line_append(
				line, sizeof(line), off,
				" addr=%llx len=%llx vec_ptr=%llx",
				(unsigned long long)a1, (unsigned long long)a2,
				(unsigned long long)a3);
			break;
		case __NR_execve:
			off = probe_line_append(line, sizeof(line), off,
						" path_ptr=%llx",
						(unsigned long long)a1);
			break;
		default:
			break;
		}

		klog_debug_fmt("PROBE", "%s", line);
	}
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

	klog_debug_fmt("PROBE",
		       "[PROBE][PTE] %s va=%llx mapped=%x present=%x user=%x rw=%x nx=%x",
		       tag ? tag : "?", (unsigned long long)va,
		       (unsigned)(mapped > 0 ? 1U : 0U),
		       (unsigned)((mapped > 0 && pte && (*pte & PAGE_PRESENT)) ?
					  1U :
					  0U),
		       (unsigned)(pte_flags & PAGE_USER ? 1U : 0U),
		       (unsigned)(pte_flags & PAGE_RW ? 1U : 0U),
		       (unsigned)(pte && (*pte & PAGE_NX) ? 1U : 0U));
}

static void probe_dump_vma_region(const char *kind, uint64_t start, uint64_t end,
				  int prot, int flags, const char *rel)
{
	char prot_s[8];
	char flg_s[32];

	probe_format_prot(prot, prot_s, sizeof(prot_s));
	if (flags >= 0)
	{
		probe_format_mmap_flags(flags, flg_s, sizeof(flg_s));
		klog_debug_fmt("PROBE",
			       "[PROBE][VMA] %s %s [%llx,%llx) prot=%s flags=%s",
			       rel ? rel : "?", kind, (unsigned long long)start,
			       (unsigned long long)end, prot_s, flg_s);
	}
	else
		klog_debug_fmt("PROBE",
			       "[PROBE][VMA] %s %s [%llx,%llx) prot=%s",
			       rel ? rel : "?", kind, (unsigned long long)start,
			       (unsigned long long)end, prot_s);
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

	klog_debug_fmt("PROBE",
		       "[PROBE][VMA] focus_cr2=%llx pid=%x comm=%s",
		       (unsigned long long)fa, (unsigned)(uint32_t)p->task.pid,
		       p->comm[0] ? p->comm : "(none)");

	if (fa >= p->heap_start && fa < p->heap_end)
		klog_debug_fmt("PROBE", "[PROBE][VMA] IN_HEAP=1");
	else if (fa >= p->stack_start && fa < p->stack_start + p->stack_size)
		klog_debug_fmt("PROBE", "[PROBE][VMA] IN_STACK=1");
	else
		klog_debug_fmt("PROBE", "[PROBE][VMA] IN_HEAP=0 IN_STACK=0");

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
		klog_debug_fmt("PROBE",
			       "[PROBE][VMA] HIT none (cr2 outside mmap VMA list)");

	if (prev_mmap)
	{
		uint64_t s = (uint64_t)(uintptr_t)prev_mmap->addr;
		uint64_t e = s + (uint64_t)prev_mmap->length;

		probe_dump_vma_region("mmap", s, e, prev_mmap->prot,
				      prev_mmap->flags, "PREV");
	}
	else
		klog_debug_fmt("PROBE", "[PROBE][VMA] PREV none");

	if (next_mmap)
	{
		uint64_t s = (uint64_t)(uintptr_t)next_mmap->addr;
		uint64_t e = s + (uint64_t)next_mmap->length;

		probe_dump_vma_region("mmap", s, e, next_mmap->prot,
				      next_mmap->flags, "NEXT");
	}
	else
		klog_debug_fmt("PROBE", "[PROBE][VMA] NEXT none");

	if (fa >= PROBE_MUSL_ARENA && fa < PROBE_MUSL_ARENA + 0x200000UL)
		klog_debug_fmt("PROBE", "[PROBE][VMA] in_musl_arena=1");
}

static void probe_dump_sc_ring(void)
{
	uint32_t count;
	uint32_t i;

	if (probe_sc_head == 0)
	{
		klog_debug_fmt("PROBE", "[PROBE][SC_RING] empty");
		return;
	}

	count = probe_sc_head;
	if (count > PROBE_SC_RING_CAP)
		count = PROBE_SC_RING_CAP;

	klog_debug_fmt("PROBE",
		       "[PROBE][SC_RING] last %x syscalls pid=%x", (unsigned)count,
		       (unsigned)probe_target_pid);

	for (i = 0; i < count; i++)
	{
		uint32_t idx = (probe_sc_head - count + i) % PROBE_SC_RING_CAP;
		const struct probe_sc_entry *e = &probe_sc_ring[idx];

		klog_debug_fmt("PROBE",
			       "  #%x %s(%llx) rip=%llx a0=%llx a1=%llx a2=%llx ret=%llx",
			       (unsigned)e->seq, probe_sc_name(e->nr),
			       (unsigned long long)e->nr,
			       (unsigned long long)e->rip,
			       (unsigned long long)e->a0,
			       (unsigned long long)e->a1,
			       (unsigned long long)e->a2,
			       (unsigned long long)(uint64_t)e->ret);
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

	{
		char line[PROBE_SC_LINE_SZ];
		size_t off = 0;
		char prot_s[8];

		off = probe_line_append(
			line, sizeof(line), off,
			"[PROBE][SC] post pid=%x nr=%llx %s ret=%llx",
			(unsigned)probe_target_pid, (unsigned long long)nr,
			probe_sc_name(nr), (unsigned long long)(uint64_t)ret);

		if (ret < 0 && ret > -4096)
			off = probe_line_append(line, sizeof(line), off,
						" errno=%llx",
						(unsigned long long)(uint64_t)(-ret));

		if (nr == __NR_mincore && ret < 0)
			off = probe_line_append(line, sizeof(line), off,
						" (mincore_unimplemented_or_fail)");

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
					off = probe_line_append(
						line, sizeof(line), off,
						" handler_after=%llx sa_flags=%x",
						(unsigned long long)(uint64_t)(
							uintptr_t)current_process
							->signal_handlers[signum],
						(unsigned)current_process
							->signal_sa_flags[signum]);
				}
			}
			break;
		case __NR_mmap:
			if (ret >= 0)
			{
				off = probe_line_append(line, sizeof(line), off,
							" map_at=%llx",
							(unsigned long long)(uint64_t)ret);
				hit = probe_vma_containing(current_process,
							   (uint64_t)ret);
				if (hit)
				{
					probe_format_prot(hit->prot, prot_s,
							  sizeof(prot_s));
					off = probe_line_append(
						line, sizeof(line), off,
						" vma_ok=1 prot=%s", prot_s);
				}
				else
					off = probe_line_append(line, sizeof(line),
								off, " vma_ok=0");
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
					probe_dump_pte(current_process, a0,
						       "mprotect_post");
					hit = probe_vma_containing(current_process,
								   a0);
					if (hit)
					{
						probe_format_prot(hit->prot, prot_s,
								  sizeof(prot_s));
						off = probe_line_append(
							line, sizeof(line), off,
							" vma_prot=%s", prot_s);
					}
				}
				(void)a1;
			}
			break;
		case __NR_brk:
			if (current_process)
			{
				off = probe_line_append(
					line, sizeof(line), off,
					" heap=[%llx,%llx)",
					(unsigned long long)current_process
						->heap_start,
					(unsigned long long)current_process
						->heap_end);
			}
			break;
		default:
			break;
		}

		klog_debug_fmt("PROBE", "%s", line);
	}
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

	klog_debug_fmt("PROBE",
		       "=== [PROBE][PF_DUMP] D1.3 musl probe diagnostics ===");
	klog_debug_fmt("PROBE",
		       "[PROBE][PF] cr2=%llx rip=%llx pid=%x handler_segv=%llx proc_mask=%x ignored=%x",
		       (unsigned long long)fault_addr,
		       (unsigned long long)fault_rip,
		       (unsigned)(uint32_t)p->task.pid,
		       (unsigned long long)(uint64_t)(uintptr_t)
			       p->signal_handlers[SIGSEGV],
		       (unsigned)p->signal_mask, (unsigned)p->signal_ignored);

	probe_dump_vma_window(p, fault_addr);
	probe_dump_all_vmas(p);
	probe_dump_pte(p, fault_addr, "cr2");
	probe_dump_pte(p, fault_addr & ~0xFFFULL, "cr2_page");
	if (fault_addr >= 0x1000UL)
		probe_dump_pte(p, fault_addr - 0x1000UL, "page_before");
	probe_dump_pte(p, (fault_addr & ~0xFFFULL) + 0x1000UL, "page_after");

	klog_debug_fmt("PROBE",
		       "[PROBE][ANSWERS] sigsegv_handler_registered=%s",
		       signals_has_user_handler(p, SIGSEGV) ? "1" : "0");

	probe_dump_sc_ring();
	ktm_flight_dump_last(64);
	klog_debug_fmt("PROBE", "=== [PROBE][PF_DUMP] end ===");
}

#endif /* CONFIG_KTM_PROBE_DIAG */
