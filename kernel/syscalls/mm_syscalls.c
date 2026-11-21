/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls.c
 * Description: mmap, munmap and mprotect wrappers, private file mmap and sysinfo.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mm_syscalls.h"
#include <mm/map_sys.h>
#include <ir0/abi/mmap_contract.h>
#include <config.h>
#include <ir0/vfs.h>
#include <ir0/kmem.h>
#include <ir0/ktm/klog.h>
#include <ir0/paging.h>
#include <ir0/mm_port.h>
#include <ir0/process.h>
#include <ir0/mman.h>
#include <ir0/clock.h>
#include <ir0/ktm/checkpoint.h>
#include <string.h>
#include "mm_syscalls_internal.h"

static bool mm_va_range_all_unmapped(address_space_root_t root, uintptr_t start,
				     size_t length)
{
	uintptr_t check;

	if (!root || length == 0)
		return false;

	for (check = start; check < start + length; check += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(root, check, NULL) == 1)
			return false;
	}
	return true;
}

static uintptr_t mm_mmap_search_low(process_t *proc)
{
	uintptr_t search_low = USER_MMAP_START;

	if (!proc)
		return search_low;

	if (process_heap_end(proc) > process_heap_start(proc) &&
	    process_heap_end(proc) > search_low)
	{
		search_low = (uintptr_t)(process_heap_end(proc) + PAGE_SIZE_4KB - 1) &
			     ~(PAGE_SIZE_4KB - 1);
	}
	return search_low;
}

static uintptr_t mm_mmap_stack_ref(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE || !process_stack_start(proc))
		return USER_STACK_BASE;

	return (uintptr_t)process_stack_start(proc);
}

static uintptr_t mm_mmap_search_end(process_t *proc)
{
	if (!proc)
		return USER_MMAP_END;

	return ir0_mmap_stack_search_end(mm_mmap_stack_ref(proc));
}

/*
 * Linux-like top-down placement for mmap(NULL) and non-fixed hints.
 * Updates process_mmap_base(proc) to the chosen start for the next call.
 */
uintptr_t mm_pick_free_va_topdown(process_t *proc, address_space_root_t root,
					 size_t length)
{
	uintptr_t search_end;
	uintptr_t search_low;
	uintptr_t top;
	uintptr_t start;

	if (!proc || !root || length == 0)
		return 0;

	if (length > (size_t)(USER_MMAP_END - USER_MMAP_START))
		return 0;

	{
		uintptr_t stack_ref = mm_mmap_stack_ref(proc);

		search_low = mm_mmap_search_low(proc);
		search_end = mm_mmap_search_end(proc);
		if (search_low >= search_end || length > search_end - search_low)
			return 0;

		top = (uintptr_t)process_mmap_base(proc);
		if (top == 0 || top > search_end)
			top = search_end;
		for (start = top - length; start >= search_low; start -= PAGE_SIZE_4KB)
		{
			start &= ~(PAGE_SIZE_4KB - 1);
			if (start < search_low)
				break;
			if (!ir0_mmap_respects_stack_gap(start, length, stack_ref))
				continue;
			if (process_user_va_range_overlaps(proc, start, length))
				continue;
			if (!mm_va_range_all_unmapped(root, start, length))
				continue;

			process_set_mmap_base(proc, start);
			return start;
		}
	}
	return 0;
}

static uintptr_t mm_find_free_va(address_space_root_t root, process_t *proc,
				 uintptr_t hint,
				 size_t length)
{
	if (hint != 0)
	{
		if ((hint & (PAGE_SIZE_4KB - 1)) != 0)
			return 0;
		if (hint < USER_MMAP_START ||
		    hint + length > USER_MMAP_END || hint + length < hint)
			return 0;
		if (!mm_user_range_mappable(hint, length))
			return 0;
		if (proc && process_user_va_range_overlaps(proc, hint, length))
			return 0;
		if (is_page_mapped_in_directory(root, hint, NULL) == 1)
			return 0;
		return hint;
	}

	return mm_pick_free_va_topdown(proc, root, length);
}

void *mm_mmap_file_private(process_t *proc, void *addr, size_t length, int prot,
                           int flags, int fd, off_t offset)
{
	fd_entry_t *fd_table;
	struct vfs_file *vfs_file;
	stat_t st;
	size_t map_len;
	size_t file_rem;
	uint64_t page_flags;
	uintptr_t virt_addr;
	struct mmap_region *region;
	char page_buf[PAGE_SIZE_4KB];
	size_t copied;
	off_t file_pos;

	if (!proc)
		return SYSCALL_PTR_ERR(ESRCH);

	if (flags & MAP_SHARED)
	{
		if (prot & PROT_WRITE)
			return SYSCALL_PTR_ERR(ENOSYS);
	}

	fd_table = process_fd_table(proc);
	if (fd < 0 || fd >= process_fd_span(proc) || !fd_table[fd].in_use)
		return SYSCALL_PTR_ERR(EBADF);
	if (fd_table[fd].is_pipe || fd_table[fd].is_devfs)
		return SYSCALL_PTR_ERR(ENODEV);
	if (!fd_table[fd].vfs_file)
		return SYSCALL_PTR_ERR(EACCES);

	if (vfs_stat(fd_table[fd].path, &st) != 0)
		return SYSCALL_PTR_ERR(EACCES);
	if (!S_ISREG(st.st_mode))
		return SYSCALL_PTR_ERR(ENODEV);

	if (offset < 0 || (uint64_t)offset >= (uint64_t)st.st_size)
		return SYSCALL_PTR_ERR(EINVAL);

	file_rem = (size_t)((uint64_t)st.st_size - (uint64_t)offset);
	map_len = length;
	if (map_len > file_rem)
		map_len = file_rem;
	map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
	if (map_len == 0)
		return SYSCALL_PTR_ERR(EINVAL);

	virt_addr = mm_find_free_va(process_pgd(proc), proc, (uintptr_t)addr, map_len);
	if (virt_addr == 0)
		return SYSCALL_PTR_ERR(ENOMEM);

	page_flags = PAGE_USER;
	if (prot & PROT_WRITE)
		page_flags |= PAGE_RW;
	if (prot & PROT_EXEC)
		page_flags |= PAGE_EXEC;

	/*
	 * Fill through a writable mapping, then apply @prot: the uaccess copy
	 * only writes into user pages the process could write itself.
	 */
	if (map_user_region_in_directory(process_pgd(proc), virt_addr, map_len,
					 PAGE_USER | PAGE_RW) != 0)
		return SYSCALL_PTR_ERR(ENOMEM);

	vfs_file = (struct vfs_file *)fd_table[fd].vfs_file;
	copied = 0;
	file_pos = offset;

	while (copied < map_len)
	{
		size_t chunk = map_len - copied;
		int nread;

		if (chunk > sizeof(page_buf))
			chunk = sizeof(page_buf);
		nread = vfs_pread(vfs_file, page_buf, chunk, file_pos);
		if (nread < 0)
		{
			for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
				unmap_page_in_directory(process_pgd(proc),
							virt_addr + off);
			return SYSCALL_PTR_ERR(EIO);
		}
		if ((size_t)nread < chunk)
			memset(page_buf + nread, 0, chunk - (size_t)nread);
		if (copy_to_user_mm(process_pgd(proc),
						     virt_addr + copied,
						     page_buf, chunk) != 0)
		{
			for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
				unmap_page_in_directory(process_pgd(proc),
							virt_addr + off);
			return SYSCALL_PTR_ERR(EFAULT);
		}
		copied += chunk;
		file_pos += (off_t)chunk;
	}
	/* File code copied into an executable mapping (ld.so loading a DSO). */
	if (prot & PROT_EXEC)
		paging_sync_icache_user(process_pgd(proc), virt_addr, map_len);

	if (page_flags != (PAGE_USER | PAGE_RW) &&
	    protect_user_region_in_directory(process_pgd(proc), virt_addr,
					     map_len, page_flags) != 0)
	{
		for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
			unmap_page_in_directory(process_pgd(proc), virt_addr + off);
		return SYSCALL_PTR_ERR(ENOMEM);
	}

	region = kmalloc(sizeof(struct mmap_region));
	if (!region)
	{
		for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
			unmap_page_in_directory(process_pgd(proc), virt_addr + off);
		return SYSCALL_PTR_ERR(ENOMEM);
	}

	region->addr = (void *)virt_addr;
	region->hint_addr = addr;
	region->length = map_len;
	region->prot = prot;
	region->flags = flags;
	region->next = process_mmap_list(proc);
	process_mm_set_mmap_list(proc, region);
	KTM_CHECKPOINT(KTM_CP_MM_MAP);

	if (DEBUG_MMAP_AUDIT)
		klog_debug("MMAP", "CLASSIFY FILE_MMAP_PRIVATE_OK");
	return (void *)virt_addr;
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
	return mm_mmap(addr, length, prot, flags, fd, offset);
}

int sys_munmap(void *addr, size_t length)
{
	return mm_munmap(addr, length);
}

int sys_mprotect(void *addr, size_t len, int prot)
{
	return mm_mprotect(addr, len, prot);
}

/*
 * sysinfo(2) — memory, load and uptime in one struct.
 *
 * Programs that report memory and uptime may read this syscall instead of
 * /proc. While it was missing they got -ENOSYS and showed whatever their
 * uninitialised struct held: gigabytes of RAM on a machine with megabytes,
 * and an uptime of weeks. Layout and units follow sysinfo(2) (post Linux
 * 2.3.48):
 * memory as multiples of mem_unit, loads as fixed point with a 16-bit
 * fraction.
 */
#define SYSINFO_LOAD_SHIFT 16

struct ir0_sysinfo
{
  int64_t uptime;
  uint64_t loads[3];
  uint64_t totalram;
  uint64_t freeram;
  uint64_t sharedram;
  uint64_t bufferram;
  uint64_t totalswap;
  uint64_t freeswap;
  uint16_t procs;
  uint16_t pad;
  uint64_t totalhigh;
  uint64_t freehigh;
  uint32_t mem_unit;
  char _f[20 - 2 * sizeof(uint64_t) - sizeof(uint32_t)];
};

/* Offsets checked against glibc's struct sysinfo on x86-64. */
_Static_assert(sizeof(struct ir0_sysinfo) == 112, "sysinfo ABI size");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, totalram) == 32,
               "sysinfo ABI totalram");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, procs) == 80,
               "sysinfo ABI procs");
_Static_assert(__builtin_offsetof(struct ir0_sysinfo, mem_unit) == 104,
               "sysinfo ABI mem_unit");

int64_t sys_sysinfo(void *user_info)
{
  struct ir0_sysinfo info;
  size_t total_frames = 0;
  size_t used_frames = 0;
  size_t free_frames = 0;
  uint32_t l1 = 0, l5 = 0, l15 = 0;
  unsigned runnable = 0, nprocs = 0;
  int last_pid = 0;

  if (!user_info)
    return -EFAULT;

  memset(&info, 0, sizeof(info));

  info.uptime = (int64_t)(clock_get_uptime_milliseconds() / 1000ULL);

  clock_get_loadavg(&l1, &l5, &l15, &runnable, &nprocs, &last_pid);
  /* Hundredths of a load unit into the 1/65536 fixed point Linux uses. */
  info.loads[0] = ((uint64_t)l1 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.loads[1] = ((uint64_t)l5 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.loads[2] = ((uint64_t)l15 << SYSINFO_LOAD_SHIFT) / 100ULL;
  info.procs = (uint16_t)nprocs;

  /* Same PMM figures /proc/meminfo reports, so the two cannot disagree. */
  ir0_mm_pmm_stats(&total_frames, &used_frames, &free_frames);
  info.mem_unit = (uint32_t)IR0_MM_PAGE_SIZE;
  info.totalram = (uint64_t)total_frames;
  info.freeram = (uint64_t)free_frames;

  {
    struct ir0_swap_stats st;

    ir0_swap_get_stats(&st);
    info.totalswap = st.totalswap_pages;
    info.freeswap = st.freeswap_pages;
  }

  if (copy_to_user(user_info, &info, sizeof(info)) != 0)
    return -EFAULT;

  return 0;
}

