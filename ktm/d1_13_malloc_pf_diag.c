/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: d1_13_malloc_pf_diag.c
 * Description: D1.14 mallocng/memmove PF forensics at musl memmove (D1.13 entry)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <d1_13_malloc_pf_diag.h>
#include <d1_12_read_diag.h>
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/process.h>
#include <ir0/ktm/klog.h>
#include <string.h>

#if defined(CONFIG_KTM_MALLOC_FORENSICS) && CONFIG_KTM_MALLOC_FORENSICS

#define D1_14_PAGE        4096ULL
#define D1_14_MEMMOVE_LO  0x4422B0ULL
#define D1_14_MEMMOVE_HI  0x442320ULL
#define D1_14_WIN         128

static int d1_14_sh_target(struct process *p)
{
	if (!p || !p->comm[0])
		return 0;
	return p->comm[0] == 's' && p->comm[1] == 'h' && p->comm[2] == '\0';
}

static void d1_14_hex_line(const char *tag, const uint8_t *buf, size_t len)
{
	char line[512];
	size_t off;
	size_t i;
	size_t max;

	off = (size_t)snprintf(line, sizeof(line), "%s", tag);
	max = len;
	if (max > 64)
		max = 64;
	for (i = 0; i < max && off + 4 < sizeof(line); i++)
		off += (size_t)snprintf(line + off, sizeof(line) - off, " %02x",
					buf[i]);
	klog_debug_fmt("D1.14", "%s", line);
}

static int d1_14_find_vma(struct process *p, uint64_t addr, uint64_t *lo,
			  uint64_t *hi)
{
	struct mmap_region *r;
	uint64_t stack_lo;
	uint64_t stack_hi;

	if (!p || !lo || !hi)
		return 0;

	stack_lo = (uint64_t)(uintptr_t)p->stack_start;
	stack_hi = stack_lo + (uint64_t)p->stack_size;
	if (addr >= stack_lo && addr < stack_hi)
	{
		*lo = stack_lo;
		*hi = stack_hi;
		return 1;
	}

	if (addr >= p->heap_start && addr < p->heap_end)
	{
		*lo = p->heap_start;
		*hi = p->heap_end;
		return 1;
	}

	for (r = p->mmap_list; r != NULL; r = r->next)
	{
		uint64_t start = (uint64_t)(uintptr_t)r->addr;
		uint64_t end = start + (uint64_t)r->length;

		if (addr >= start && addr < end)
		{
			*lo = start;
			*hi = end;
			return 1;
		}
	}
	return 0;
}

static int d1_14_read_clamped(struct process *p, uint64_t addr, uint8_t *out,
			      size_t len)
{
	uint64_t vma_lo;
	uint64_t vma_hi;
	uint64_t page_lo;
	uint64_t page_hi;
	uint64_t read_lo;
	uint64_t read_hi;

	if (!p || !out || len == 0 || addr < 0x1000ULL)
		return -1;

	if (!d1_14_find_vma(p, addr, &vma_lo, &vma_hi))
		return -1;

	page_lo = addr & ~(D1_14_PAGE - 1ULL);
	page_hi = page_lo + D1_14_PAGE;
	read_lo = addr;
	read_hi = addr + len;
	if (read_lo < vma_lo)
		read_lo = vma_lo;
	if (read_hi > vma_hi)
		read_hi = vma_hi;
	if (read_lo < page_lo)
		read_lo = page_lo;
	if (read_hi > page_hi)
		read_hi = page_hi;
	if (read_hi <= read_lo)
		return -1;

	memset(out, 0, len);
	return copy_from_user(out, (const void *)(uintptr_t)read_lo,
			      (size_t)(read_hi - read_lo));
}

static void d1_14_log_candidate(const char *label, uint64_t val, uint64_t rdx,
				uint64_t rcx)
{
	const char *m_rdx = "";
	const char *m_rcx = "";
	const char *m_align = "";

	if (val == rdx)
		m_rdx = " MATCH_RDX";
	if (val == (rcx * 8ULL))
		m_rcx = " MATCH_RCX8";
	if (val == (rdx & ~0xFULL))
		m_align = " MATCH_RDX_ALIGNED";
	klog_debug_fmt("D1.14", "[D1.14][WORD] %s=%llx%s%s%s", label,
		       (unsigned long long)val, m_rdx, m_rcx, m_align);
}

static void d1_14_dump_ptr_window(struct process *p, const char *label,
				  uint64_t ptr, uint64_t rdx, uint64_t rcx)
{
	uint8_t buf[D1_14_WIN];
	uint64_t vma_lo;
	uint64_t vma_hi;
	uint64_t win_lo;
	uint64_t win_hi;
	size_t n;
	uint64_t w16;
	uint64_t w8;

	if (!d1_14_find_vma(p, ptr, &vma_lo, &vma_hi))
	{
		klog_debug_fmt("D1.14", "[D1.14][WIN] %s no_vma", label);
		return;
	}

	win_lo = ptr >= D1_14_WIN ? ptr - D1_14_WIN : vma_lo;
	if (win_lo < vma_lo)
		win_lo = vma_lo;
	win_hi = ptr + D1_14_WIN;
	if (win_hi > vma_hi)
		win_hi = vma_hi;

	klog_debug_fmt("D1.14",
		       "[D1.14][WIN] %s ptr=%llx vma=[%llx,%llx) clip=[%llx,%llx)",
		       label, (unsigned long long)ptr,
		       (unsigned long long)vma_lo, (unsigned long long)vma_hi,
		       (unsigned long long)win_lo, (unsigned long long)win_hi);

	n = (size_t)(win_hi - win_lo);
	if (n > sizeof(buf))
		n = sizeof(buf);

	if (d1_14_read_clamped(p, win_lo, buf, n) != 0)
	{
		klog_debug_fmt("D1.14", "[D1.14][WIN] %s read_fail", label);
		return;
	}

	d1_14_hex_line("[D1.14][BYTES]", buf, n);

	if (ptr >= 16 && ptr - 16 >= win_lo && ptr - 8 <= win_hi)
	{
		memcpy(&w16, buf + (size_t)(ptr - 16 - win_lo), 8);
		memcpy(&w8, buf + (size_t)(ptr - 8 - win_lo), 8);
		klog_debug_fmt("D1.14", "[D1.14][HDR] %s", label);
		d1_14_log_candidate("-16", w16 & ~0xFULL, rdx, rcx);
		d1_14_log_candidate("-8", w8 & ~0xFULL, rdx, rcx);
		d1_14_log_candidate("-16_raw", w16, rdx, rcx);
		d1_14_log_candidate("-8_raw", w8, rdx, rcx);
	}
}

static void d1_14_dump_page(struct process *p, const char *label, uint64_t addr,
			    uint64_t rdx, uint64_t rcx)
{
	uint8_t page[D1_14_PAGE];
	uint64_t page_base;
	uint64_t vma_lo;
	uint64_t vma_hi;
	uint64_t w16;
	uint64_t w8;

	page_base = addr & ~(D1_14_PAGE - 1ULL);
	if (!d1_14_find_vma(p, addr, &vma_lo, &vma_hi))
	{
		klog_debug_fmt("D1.14", "[D1.14][PAGE] %s unmapped", label);
		return;
	}

	if (d1_14_read_clamped(p, page_base, page, D1_14_PAGE) != 0)
	{
		klog_debug_fmt("D1.14",
			       "[D1.14][PAGE] %s read_fail base=%llx", label,
			       (unsigned long long)page_base);
		return;
	}

	klog_debug_fmt("D1.14", "[D1.14][PAGE] %s base=%llx", label,
		       (unsigned long long)page_base);
	d1_14_hex_line("[D1.14][PAGE_BYTES]", page, 64);

	if (addr >= 16 && addr - 16 >= page_base)
	{
		memcpy(&w16, page + (addr - 16 - page_base), 8);
		memcpy(&w8, page + (addr - 8 - page_base), 8);
		d1_14_log_candidate("page@-16", w16 & ~0xFULL, rdx, rcx);
		d1_14_log_candidate("page@-8", w8 & ~0xFULL, rdx, rcx);
	}
}

void d1_13_malloc_pf_diag(struct process *p, uint64_t fault_addr, uint64_t rip,
			  uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx)
{
	if (!d1_14_sh_target(p))
		return;
	if (rip < D1_14_MEMMOVE_LO || rip > D1_14_MEMMOVE_HI)
		return;

	klog_debug_fmt("D1.14", "=== [D1.14][MALLOC_PF] ===");
	klog_debug_fmt("D1.14",
		       "[D1.14][REGS] rip=%llx cr2=%llx rdi=%llx rsi=%llx rdx=%llx rcx=%llx",
		       (unsigned long long)rip, (unsigned long long)fault_addr,
		       (unsigned long long)rdi, (unsigned long long)rsi,
		       (unsigned long long)rdx, (unsigned long long)rcx);

	if (rip == 0x4422E3ULL)
		klog_debug_fmt("D1.14", "[D1.14][SITE] rep_movsq");

	d1_14_dump_page(p, "rdi_page", rdi, rdx, rcx);
	d1_14_dump_page(p, "rsi_page", rsi, rdx, rcx);
	d1_14_dump_ptr_window(p, "rdi", rdi, rdx, rcx);
	d1_14_dump_ptr_window(p, "rsi", rsi, rdx, rcx);

	d1_12_read_diag_pf(p, rip, rdx, rsi, rdi);
	klog_debug_fmt("D1.14", "=== [D1.14][MALLOC_PF] end ===");
}

#else /* !CONFIG_KTM_MALLOC_FORENSICS */

void d1_13_malloc_pf_diag(struct process *p, uint64_t fault_addr, uint64_t rip,
			  uint64_t rdi, uint64_t rsi, uint64_t rdx, uint64_t rcx)
{
	(void)p;
	(void)fault_addr;
	(void)rip;
	(void)rdi;
	(void)rsi;
	(void)rdx;
	(void)rcx;
}

#endif /* CONFIG_KTM_MALLOC_FORENSICS */
