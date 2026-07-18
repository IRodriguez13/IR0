/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls.c
 * Description: memory-management syscall helpers (split from syscalls.c)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/syscalls_kernel.h>
#include "mm_syscalls.h"
#include "syscalls_glue.h"
#include <ir0/abi/mmap_contract.h>
#include <config.h>
#include <ir0/vfs.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/serial_io.h>
#include <ir0/stat.h>
#include <ir0/process.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/arch_port.h>
#include <ir0/ktm/checkpoint.h>
#include <stdbool.h>
#include <string.h>

#include <ir0/fb.h>
#include <ir0/validation.h>
#include <stdint.h>

#define MAP_SHARED    0x01
#define MAP_PRIVATE   0x02
#define MAP_FIXED     0x10
#define MAP_ANONYMOUS 0x20
#define PROT_READ   0x1
#define PROT_WRITE  0x2
#define PROT_EXEC   0x4
#define PROT_NONE   0x0
#define SYSCALL_PTR_ERR(err) ((void *)(intptr_t)(-(err)))
#define MMAP_AUDIT_FAILED ((void *)(intptr_t)-1)

static void fase39_dump_current_vmas(const char *tag);

static void mm_prepare_map_fixed(uintptr_t start, size_t length)
{
	struct mmap_region **link;
	uintptr_t end;

	if (!current_process || length == 0)
		return;

	end = start + length;
	link = &current_process->mmap_list;

	while (*link)
	{
		struct mmap_region *region = *link;
		uintptr_t region_start = (uintptr_t)region->addr & ~(PAGE_SIZE_4KB - 1);
		uintptr_t region_end = region_start +
			((region->length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1));

		if (region_end <= start || region_start >= end)
		{
			link = &region->next;
			continue;
		}

		*link = region->next;
		kfree(region);
	}

	for (uintptr_t page = start; page < end; page += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(current_process->page_directory, page,
						NULL) == 1)
			unmap_page_in_directory(current_process->page_directory, page);
	}
}

static int mm_mmap_verify_ptes(uint64_t *pml4, uintptr_t virt_addr, size_t len)
{
	size_t i;

	if (!pml4 || len == 0)
		return -EINVAL;

	for (i = 0; i < len; i += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(pml4, virt_addr + i, NULL) != 1)
			return -ENOMEM;
	}
	return 0;
}

static bool mm_va_range_all_unmapped(uint64_t *pml4, uintptr_t start,
				     size_t length)
{
	uintptr_t check;

	if (!pml4 || length == 0)
		return false;

	for (check = start; check < start + length; check += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(pml4, check, NULL) == 1)
			return false;
	}
	return true;
}

static uintptr_t mm_mmap_search_low(process_t *proc)
{
	uintptr_t search_low = USER_MMAP_START;

	if (!proc)
		return search_low;

	if (proc->heap_end > proc->heap_start &&
	    proc->heap_end > search_low)
	{
		search_low = (uintptr_t)(proc->heap_end + PAGE_SIZE_4KB - 1) &
			     ~(PAGE_SIZE_4KB - 1);
	}
	return search_low;
}

static uintptr_t mm_mmap_stack_ref(process_t *proc)
{
	if (!proc || proc->mode != USER_MODE || !proc->stack_start)
		return USER_STACK_BASE;

	return (uintptr_t)proc->stack_start;
}

static uintptr_t mm_mmap_search_end(process_t *proc)
{
	if (!proc)
		return USER_MMAP_END;

	return ir0_mmap_stack_search_end(mm_mmap_stack_ref(proc));
}

/*
 * Linux-like top-down placement for mmap(NULL) and non-fixed hints.
 * Updates proc->mmap_base to the chosen start for the next call.
 */
static uintptr_t mm_pick_free_va_topdown(process_t *proc, uint64_t *pml4,
					 size_t length)
{
	uintptr_t search_end;
	uintptr_t search_low;
	uintptr_t top;
	uintptr_t start;

	if (!proc || !pml4 || length == 0)
		return 0;

	if (length > (size_t)(USER_MMAP_END - USER_MMAP_START))
		return 0;

	{
		uintptr_t stack_ref = mm_mmap_stack_ref(proc);

		search_low = mm_mmap_search_low(proc);
		search_end = mm_mmap_search_end(proc);
		if (search_low >= search_end || length > search_end - search_low)
			return 0;

		top = (uintptr_t)proc->mmap_base;
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
			if (!mm_va_range_all_unmapped(pml4, start, length))
				continue;

			proc->mmap_base = start;
			return start;
		}
	}
	return 0;
}

static uintptr_t mm_find_free_va(uint64_t *pml4, process_t *proc, uintptr_t hint,
				 size_t length)
{
	if (hint != 0)
	{
		if ((hint & (PAGE_SIZE_4KB - 1)) != 0)
			return 0;
		if (!is_user_address((void *)(uintptr_t)hint, length))
			return 0;
		if (proc && process_user_va_range_overlaps(proc, hint, length))
			return 0;
		if (is_page_mapped_in_directory(pml4, hint, NULL) == 1)
			return 0;
		return hint;
	}

	return mm_pick_free_va_topdown(proc, pml4, length);
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

	fd_table = proc->fd_table;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS || !fd_table[fd].in_use)
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

	virt_addr = mm_find_free_va(proc->page_directory, proc, (uintptr_t)addr, map_len);
	if (virt_addr == 0)
		return SYSCALL_PTR_ERR(ENOMEM);

	page_flags = PAGE_USER;
	if (prot & PROT_WRITE)
		page_flags |= PAGE_RW;
	if (prot & PROT_EXEC)
		page_flags |= PAGE_EXEC;

	if (map_user_region_in_directory(proc->page_directory, virt_addr, map_len,
					 page_flags) != 0)
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
				unmap_page_in_directory(proc->page_directory,
							virt_addr + off);
			return SYSCALL_PTR_ERR(EIO);
		}
		if ((size_t)nread < chunk)
			memset(page_buf + nread, 0, chunk - (size_t)nread);
		if (copy_to_user_region_in_directory(proc->page_directory,
						     virt_addr + copied,
						     page_buf, chunk) != 0)
		{
			for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
				unmap_page_in_directory(proc->page_directory,
							virt_addr + off);
			return SYSCALL_PTR_ERR(EFAULT);
		}
		copied += chunk;
		file_pos += (off_t)chunk;
	}

	region = kmalloc_try(sizeof(struct mmap_region));
	if (!region)
	{
		for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
			unmap_page_in_directory(proc->page_directory, virt_addr + off);
		return SYSCALL_PTR_ERR(ENOMEM);
	}

	region->addr = (void *)virt_addr;
	region->hint_addr = addr;
	region->length = map_len;
	region->prot = prot;
	region->flags = flags;
	region->next = proc->mmap_list;
	proc->mmap_list = region;
	KTM_CHECKPOINT(KTM_CP_MM_MAP);

	serial_print("[MMAP_AUDIT][CLASSIFY] FILE_MMAP_PRIVATE_OK\n");
	return (void *)virt_addr;
}

int64_t sys_brk(void *addr)
{
	uintptr_t heap_lo;
	uintptr_t new_brk;
	uintptr_t current_brk;

	if (!current_process)
		return -ESRCH;

	/* brk(NULL) / brk(0): return current program break (Linux ABI). */
	if (!addr)
	{
		if (current_process->heap_start == 0 &&
		    current_process->heap_end == 0)
		{
			current_process->heap_start = USER_HEAP_BASE;
			current_process->heap_end = USER_HEAP_BASE;
		}
		return (int64_t)current_process->heap_end;
	}

	if (!is_user_address(addr, 0))
		return -EFAULT;

	new_brk = (uintptr_t)addr;
	current_brk = current_process->heap_end;
	heap_lo = current_process->heap_start;

	/*
	 * Processes without ELF exec init (legacy smokes): fall back to
	 * USER_HEAP_BASE. Post-exec images set heap_start/end from PT_LOAD.
	 */
	if (heap_lo == 0 && current_brk == 0)
	{
		heap_lo = USER_HEAP_BASE;
		current_process->heap_start = heap_lo;
		current_process->heap_end = heap_lo;
		current_brk = heap_lo;
	}

	if (new_brk < heap_lo)
		return -EFAULT;
	if (new_brk > heap_lo + USER_HEAP_MAX_SIZE)
		return -EFAULT;

	/* If expanding heap, map new pages. */
	if (new_brk > current_brk)
	{
		uintptr_t start_page = current_brk & (uintptr_t)PAGE_FRAME_MASK;
		uintptr_t end_page = (new_brk + 0xFFF) & ~0xFFF;
		size_t size_to_map = end_page - start_page;

		if (size_to_map > 0)
		{
			if (map_user_region_in_directory(current_process->page_directory,
							 start_page, size_to_map,
							 PAGE_RW) != 0)
				return (int64_t)current_process->heap_end;
		}
	}
	else if (new_brk < current_brk)
	{
		uintptr_t old_end = current_brk;

		for (uintptr_t page = (new_brk + (PAGE_SIZE_4KB - 1)) &
				      (uintptr_t)PAGE_FRAME_MASK;
		     page < old_end;
		     page += PAGE_SIZE_4KB)
			unmap_page_in_directory(current_process->page_directory, page);
	}

	current_process->heap_end = new_brk;
	fase39_dump_current_vmas("brk");
	return (int64_t)new_brk;
}

/* sbrk is typically implemented as a userspace library function using brk */
/* POSIX does not require sbrk as a syscall */


static int mmap_audit_ptr_err(void *ret)
{
  return ((intptr_t)ret < 0);
}

static int mmap_audit_errno_from_ret(void *ret)
{
  if (!mmap_audit_ptr_err(ret))
    return 0;
  return -(int)(intptr_t)ret;
}

static void mmap_audit_log_pte(const char *tag, uint64_t *pml4, uintptr_t va)
{
  uint64_t pte_flags = 0;
  uint64_t *pte;
  int mapped;

  if (!DEBUG_MMAP_AUDIT)
    return;
  if (!pml4)
    return;

  mapped = is_page_mapped_in_directory(pml4, va, &pte_flags);
  pte = paging_get_pte(pml4, va);

  serial_print("[MMAP_AUDIT][PTE] tag=");
  serial_print(tag ? tag : "(null)");
  serial_print(" va=");
  serial_print_hex64((uint64_t)va);
  serial_print(" mapped=");
  serial_print_hex64((uint64_t)(mapped > 0 ? 1 : 0));
  serial_print(" present=");
  serial_print_hex64((uint64_t)(pte && (*pte & PAGE_PRESENT) ? 1 : 0));
  serial_print(" user=");
  serial_print_hex64((uint64_t)(pte_flags & PAGE_USER ? 1 : 0));
  serial_print(" rw=");
  serial_print_hex64((uint64_t)(pte_flags & PAGE_RW ? 1 : 0));
  serial_print(" nx=");
  serial_print_hex64((uint64_t)(pte && (*pte & PAGE_NX) ? 1 : 0));
  if (pte && (*pte & PAGE_PRESENT))
  {
    serial_print(" pfn=");
    serial_print_hex64((uint64_t)(*pte & PAGE_PTE_PFN_MASK));
  }
  serial_print("\n");
}

static void mmap_audit_log_args(void *addr, size_t length, int prot, int flags,
                                int fd, off_t offset)
{
  if (!DEBUG_MMAP_AUDIT)
    return;
  serial_print("[MMAP_AUDIT][ARGS] classify=MMAP_ARGS_DECODED pid=");
  serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
  serial_print(" comm=");
  serial_print(current_process ? current_process->comm : "(none)");
  serial_print(" addr=");
  serial_print_hex64((uint64_t)(uintptr_t)addr);
  serial_print(" length=");
  serial_print_hex64((uint64_t)length);
  serial_print(" prot=");
  serial_print_hex64((uint64_t)(unsigned int)prot);
  serial_print(" flags=");
  serial_print_hex64((uint64_t)(unsigned int)flags);
  serial_print(" fd=");
  serial_print_hex64((uint64_t)(unsigned int)fd);
  serial_print(" offset=");
  serial_print_hex64((uint64_t)offset);
  if (current_process)
  {
    serial_print(" caller_rip=");
    serial_print_hex64(current_process->syscall_frame.rip);
    serial_print(" caller_rsp=");
    serial_print_hex64(current_process->syscall_frame.rsp);
  }
  serial_print("\n");

  if ((flags & MAP_SHARED) != 0 && (flags & MAP_PRIVATE) != 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_UNSUPPORTED_FLAGS reason=MAP_SHARED_and_MAP_PRIVATE\n");
  }
  if (!(flags & MAP_ANONYMOUS) && fd < 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_UNSUPPORTED_FLAGS reason=file_map_without_fd\n");
  }
}

static void mmap_audit_log_return(const char *stage, void *ret, uintptr_t virt_addr,
                                  size_t length, int vma_inserted, uint64_t *pml4)
{
  size_t pages = (length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
  size_t mapped_pages = 0;
  size_t i;

  if (!DEBUG_MMAP_AUDIT)
    return;

  serial_print("[MMAP_AUDIT][RET] stage=");
  serial_print(stage ? stage : "(null)");
  serial_print(" ret=");
  serial_print_hex64((uint64_t)(uintptr_t)ret);
  if (mmap_audit_ptr_err(ret))
  {
    serial_print(" errno=");
    serial_print_hex64((uint64_t)(unsigned int)mmap_audit_errno_from_ret(ret));
  }
  else
  {
    serial_print(" range=[");
    serial_print_hex64((uint64_t)virt_addr);
    serial_print(",");
    serial_print_hex64((uint64_t)(virt_addr + length));
    serial_print(") pages=");
    serial_print_hex64((uint64_t)pages);
  }
  serial_print(" vma_inserted=");
  serial_print_hex64((uint64_t)(unsigned int)vma_inserted);
  serial_print("\n");

  if (mmap_audit_ptr_err(ret) || !pml4 || virt_addr == 0 || length == 0)
    return;

  for (i = 0; i < pages; i++)
  {
    uintptr_t va = virt_addr + i * PAGE_SIZE_4KB;
    if (is_page_mapped_in_directory(pml4, va, NULL) == 1)
      mapped_pages++;
  }

  serial_print("[MMAP_AUDIT][RET] pte_mapped_pages=");
  serial_print_hex64((uint64_t)mapped_pages);
  serial_print(" pte_expected_pages=");
  serial_print_hex64((uint64_t)pages);
  serial_print("\n");

  if (mapped_pages == 0)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_VMA_WITHOUT_PTES\n");
  }
  else if (mapped_pages < pages)
  {
    serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_RET_UNMAPPED_RANGE\n");
  }

  mmap_audit_log_pte("first", pml4, virt_addr);
  if (pages > 1)
    mmap_audit_log_pte("last", pml4, virt_addr + (pages - 1) * PAGE_SIZE_4KB);

  if (pml4)
  {
    uint64_t flags_low = 0;

    if (is_page_mapped_in_directory(pml4, virt_addr, &flags_low) == 1)
    {
      if (!(flags_low & PAGE_USER))
      {
        serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_USER\n");
      }
      if (!(flags_low & PAGE_RW))
      {
        serial_print("[MMAP_AUDIT][CLASSIFY] MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_RW\n");
      }
    }
  }
}

/*
 * FASE39 diagnostics: dump current process VMAs after brk/mmap/munmap.
 * Pure observability helper (no policy changes).
 */
static void fase39_dump_current_vmas(const char *tag)
{
  struct mmap_region *r;

  if (!DEBUG_MMAP_AUDIT)
    return;
  if (!current_process)
    return;




  for (r = current_process->mmap_list; r; r = r->next)
  {
    if ((r->flags & MAP_ANONYMOUS) != 0)
      serial_print("anonymous");
    else
      serial_print("fd-backed-or-device");
    serial_print("\n");
  }
}

void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *ret;
  uintptr_t virt_addr_out = 0;
  size_t aligned_len = 0;
  int vma_inserted = 0;

  mmap_audit_log_args(addr, length, prot, flags, fd, offset);

  if (!current_process)
  {
    serial_print("SERIAL: mmap: no current process\n");
    ret = (void *)(intptr_t)-ESRCH;
    mmap_audit_log_return("no-process", ret, 0, 0, 0, NULL);
    return ret;
  }

  if (length == 0)
  {
    serial_print("SERIAL: mmap: zero length\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("zero-length", ret, 0, 0, 0, current_process->page_directory);
    return ret;
  }

  if (length > (USER_MMAP_END - USER_MMAP_START) || length > (1UL << 28))
  {
    serial_print("SERIAL: mmap: length exceeds user mmap arena\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("length-too-large", ret, 0, 0, 0,
                          current_process->page_directory);
    return ret;
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    serial_print("SERIAL: mmap: invalid protection flags\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("bad-prot", ret, 0, 0, 0, current_process->page_directory);
    return ret;
  }

  /* Validate offset alignment for file mappings */
  if (!(flags & MAP_ANONYMOUS))
  {
    if (fd < 0)
    {
      serial_print("SERIAL: mmap: file mapping requires valid fd\n");
      return SYSCALL_PTR_ERR(EBADF);
    }
    
    /* Offset must be page-aligned for file mappings */
    if (offset % PAGE_SIZE_4KB != 0)
    {
      serial_print("SERIAL: mmap: offset must be page-aligned\n");
      return SYSCALL_PTR_ERR(EINVAL);
    }

    /*
     * Linux: reject invalid fds before ENOSYS for unimplemented file-backed
     * mmap. Only real fd_table slots are valid (devfs /dev/fb0 uses is_devfs).
     */
    {
      fd_entry_t *fd_table = get_process_fd_table();
      bool fd_valid_open = (fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
                            fd_table && fd_table[fd].in_use);

      if (!fd_valid_open)
      {
        serial_print("SERIAL: mmap: invalid fd for file-backed mmap\n");
        ret = SYSCALL_PTR_ERR(EBADF);
        mmap_audit_log_return("bad-fd", ret, 0, 0, 0,
                              current_process->page_directory);
        return ret;
      }
    }

    /*
     * mmap of /dev/fb0 — real fd_table slot (is_devfs + device_id 15).
     * Maps framebuffer physical memory into userspace for efficient access.
     */
    {
      bool fb_mmap_devfs = false;
      uint32_t device_id = UINT32_MAX;
      fd_entry_t *fd_table = get_process_fd_table();

      if (fd >= 0 && fd < MAX_FDS_PER_PROCESS &&
          fd_table && fd_table[fd].in_use && fd_table[fd].is_devfs)
      {
        device_id = fd_table[fd].dev_device_id;
        fb_mmap_devfs = true;
      }

#if CONFIG_ENABLE_VBE
      if (device_id == 15U)
      {
        struct ir0_fb_info fb_info;
        uint32_t fb_phys;
        uint32_t fb_size;

        if (!ir0_fb_get_info(&fb_info))
          return SYSCALL_PTR_ERR(ENODEV);

        fb_phys = fb_info.fb_phys;
        fb_size = fb_info.fb_size;
        if (fb_phys == 0 || fb_size == 0)
          return SYSCALL_PTR_ERR(ENODEV);

        if (offset < 0 || (uint64_t)offset >= (uint64_t)fb_size)
          return SYSCALL_PTR_ERR(EINVAL);

        uint64_t off_u = (uint64_t)offset;
        uint64_t rem = (uint64_t)fb_size - off_u;
        size_t map_len = length;

        if ((uint64_t)map_len > rem)
          map_len = (size_t)rem;
        map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
        if (map_len == 0)
          return SYSCALL_PTR_ERR(EINVAL);

        uintptr_t virt_addr = 0;
        if (addr != NULL)
        {
          uintptr_t hint_addr = (uintptr_t)addr;
          if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
            return SYSCALL_PTR_ERR(EINVAL);
          if (!is_user_address(addr, map_len))
            return SYSCALL_PTR_ERR(EFAULT);
          int mapped = is_page_mapped_in_directory(current_process->page_directory, hint_addr, NULL);
          if (mapped == 1)
            return SYSCALL_PTR_ERR(EINVAL);
          virt_addr = hint_addr;
        }
        else
        {
          uintptr_t search_start = USER_MMAP_START;
          uintptr_t search_end = USER_MMAP_END;
          uintptr_t candidate = search_start;
          bool found = false;
          while (candidate + map_len < search_end && !found)
          {
            bool all_unmapped = true;
            for (uintptr_t check = candidate; check < candidate + map_len; check += PAGE_SIZE_4KB)
            {
              int m = is_page_mapped_in_directory(current_process->page_directory, check, NULL);
              if (m == 1)
              {
                all_unmapped = false;
                candidate = ((check + PAGE_SIZE_4KB) + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
                break;
              }
            }
            if (all_unmapped)
            {
              virt_addr = candidate;
              found = true;
            }
          }
          if (!found)
            return SYSCALL_PTR_ERR(ENOMEM);
        }
        
        uint64_t page_flags = PAGE_USER | PAGE_RW;
        for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
        {
          uintptr_t v = virt_addr + off;
          uintptr_t p = (uintptr_t)fb_phys + off_u + off;
          if (map_page_in_directory(current_process->page_directory, v, p, page_flags) != 0)
          {
            /* Rollback: unmap already mapped pages */
            for (size_t r = 0; r < off; r += PAGE_SIZE_4KB)
              unmap_page_in_directory(current_process->page_directory, virt_addr + r);
            return SYSCALL_PTR_ERR(ENOMEM);
          }
        }
        
        struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
        if (!region)
        {
          for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
            unmap_page_in_directory(current_process->page_directory, virt_addr + off);
          return SYSCALL_PTR_ERR(ENOMEM);
        }
        region->addr = (void *)virt_addr;
        region->hint_addr = addr;
        region->length = map_len;
        region->prot = prot;
        region->flags = flags;
        region->next = current_process->mmap_list;
        current_process->mmap_list = region;
        KTM_CHECKPOINT(KTM_CP_MM_MAP);
        fase39_dump_current_vmas("mmap-fb");
        {
          static int s_fb_mmap_devfs_tag;
          static int s_fb_mmap_ok_tag;

          if (fb_mmap_devfs && !s_fb_mmap_devfs_tag)
          {
            s_fb_mmap_devfs_tag = 1;
            serial_print("FB_MMAP_DEVFS_FD_OK\n");
            serial_print("DEVFB0_MMAP_REAL_FD_OK\n");
          }
          if (!s_fb_mmap_ok_tag)
          {
            s_fb_mmap_ok_tag = 1;
            serial_print("FB_MMAP_OK\n");
          }
          if ((flags & MAP_SHARED) != 0)
          {
            static int s_fb_map_shared_tag;

            if (!s_fb_map_shared_tag)
            {
              s_fb_map_shared_tag = 1;
              serial_print("FB_MAP_SHARED_OK\n");
            }
          }
        }
        return (void *)virt_addr;
      }
#else
      (void)fb_mmap_devfs;
      (void)device_id;
#endif
    }
    
    /* File-based mapping not yet implemented for other files */
    serial_print("SERIAL: mmap: file-based mapping not yet implemented\n");
    return SYSCALL_PTR_ERR(ENOSYS);
  }

  /* Align length to page boundary */
  length = (length + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
  aligned_len = length;

  /* Address hint support:
   * - If addr is NULL: Kernel chooses address
   * - If addr is provided: Try to use it if valid and page-aligned
   * - If MAP_FIXED is set (future): Must use exact address
   */
  uintptr_t virt_addr = 0;
  uintptr_t hint_addr = (uintptr_t)addr;
  bool use_hint = false;

  if ((flags & MAP_FIXED) != 0)
  {
    if (addr == NULL ||
        (hint_addr & (PAGE_SIZE_4KB - 1)) != 0 ||
        !is_user_address(addr, length))
    {
      ret = SYSCALL_PTR_ERR(EINVAL);
      mmap_audit_log_return("map-fixed-bad-addr", ret, 0, 0, 0,
                            current_process->page_directory);
      return ret;
    }

    mm_prepare_map_fixed(hint_addr, length);
    virt_addr = hint_addr;
    use_hint = true;
  }
  /*
   * Non-MAP_FIXED: addr is only an advisory hint. If unusable (misaligned,
   * out of range, or already mapped) pick another address — do not EINVAL.
   */
  else if (addr != NULL &&
           (hint_addr & (PAGE_SIZE_4KB - 1)) == 0 &&
           is_user_address(addr, length))
  {
    bool collision = false;

    for (uintptr_t check = hint_addr; check < hint_addr + length;
         check += PAGE_SIZE_4KB)
    {
      if (is_page_mapped_in_directory(current_process->page_directory, check,
                                      NULL) == 1)
      {
        collision = true;
        break;
      }
    }

    if (!collision)
    {
      virt_addr = hint_addr;
      use_hint = true;
    }
  }

  if (!use_hint)
  {
    virt_addr = mm_pick_free_va_topdown(current_process,
                                        current_process->page_directory,
                                        length);
    if (virt_addr == 0)
      return SYSCALL_PTR_ERR(ENOMEM);
  }

  /* Determine page flags from protection flags */
  uint64_t page_flags = PAGE_USER;  /* Always user mode */
  if (prot & PROT_READ)
    page_flags |= 0;  /* Read is default */
  if (prot & PROT_WRITE)
    page_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    page_flags |= PAGE_EXEC;

  /* Map pages in process page directory (skip PTE install for anon PROT_NONE) */
  if (!((flags & MAP_ANONYMOUS) && prot == PROT_NONE))
  {
    if (map_user_region_in_directory(current_process->page_directory, virt_addr, length, page_flags) != 0)
    {
      serial_print("SERIAL: mmap: failed to map pages\n");
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("map-failed", ret, virt_addr, aligned_len, 0,
                            current_process->page_directory);
      return ret;
    }

    if (mm_mmap_verify_ptes(current_process->page_directory, virt_addr,
                            aligned_len) != 0)
    {
      for (uintptr_t page = virt_addr; page < virt_addr + aligned_len;
           page += PAGE_SIZE_4KB)
        unmap_page_in_directory(current_process->page_directory, page);
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("pte-verify-fail", ret, virt_addr, aligned_len, 0,
                            current_process->page_directory);
      return ret;
    }

    virt_addr_out = virt_addr;
    mmap_audit_log_pte("post-map", current_process->page_directory, virt_addr);
  }
  else if (DEBUG_MMAP_AUDIT)
  {
    serial_print("[MMAP_AUDIT][RESERVE] stage=vma-only prot_none anon len=");
    serial_print_hex64((uint64_t)aligned_len);
    serial_print("\n");
  }

  /*
   * Anonymous zero-fill: map_user_region_in_directory() clears each
   * physical frame via the identity map before installing final PTEs.
   * Do not memset() through the user VA while PTEs lack PAGE_RW — that
   * faults in kernel mode on PROT_NONE / read-only mappings.
   */
  if (DEBUG_MMAP_AUDIT)
  {
    if (prot == PROT_NONE)
    {
      serial_print("[MMAP_AUDIT][ZERO] stage=skipped reason=prot_none\n");
    }
    else if (prot & PROT_WRITE)
    {
      serial_print("[MMAP_AUDIT][ZERO] stage=phys-prezeroed-in-map_user_region\n");
    }
    else
    {
      serial_print("[MMAP_AUDIT][ZERO] stage=skipped reason=no_prot_write\n");
    }
  }

  /* Create mapping entry */
  struct mmap_region *region = kmalloc_try(sizeof(struct mmap_region));
  if (!region)
  {
      /* Failed to allocate region entry - unmap pages */
      if (!((flags & MAP_ANONYMOUS) && prot == PROT_NONE))
      {
        for (uintptr_t page = virt_addr; page < virt_addr + length; page += PAGE_SIZE_4KB)
        {
          unmap_page_in_directory(current_process->page_directory, page);
        }
      }
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  region->addr = (void *)virt_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = current_process->mmap_list;
  current_process->mmap_list = region;
  vma_inserted = 1;
  virt_addr_out = virt_addr;
  KTM_CHECKPOINT(KTM_CP_MM_MAP);
  fase39_dump_current_vmas("mmap");

  ret = (void *)virt_addr;
  mmap_audit_log_return("ok", ret, virt_addr_out, aligned_len, vma_inserted,
                        current_process->page_directory);
  if (DEBUG_MMAP_AUDIT)
    serial_print("[MMAP_AUDIT][CLASSIFY] BUSYBOX_NEXT_SYSCALL_REACHED stage=mmap-return-ok\n");
  return ret;
}

int sys_munmap(void *addr, size_t length)
{
  uint64_t unmapped_pages = 0;
  paging_ir0_mm_checkpoint("munmap-before", (int32_t)process_get_pid());
  if (!current_process)
    return -ESRCH;
  if (!addr || length == 0)
    return -EINVAL;

  /* Validate address is in userspace */
  if (!is_user_address(addr, length))
    return -EFAULT;

  /* Align to page boundaries */
  uintptr_t start_page = (uintptr_t)addr & ~0xFFF;
  size_t aligned_length = ((length + 0xFFF) & ~0xFFF);

  /* Find the mapping */
  struct mmap_region *current = current_process->mmap_list;
  struct mmap_region *prev = NULL;

  while (current)
  {
    uintptr_t mapping_start = (uintptr_t)current->addr & ~0xFFF;
    uintptr_t mapping_end = mapping_start + ((current->length + 0xFFF) & ~0xFFF);
    
    if (start_page >= mapping_start && (start_page + aligned_length) <= mapping_end)
    {
      /* Remove from list */
      if (prev)
        prev->next = current->next;
      else
        current_process->mmap_list = current->next;

      /* Unmap pages in process page directory */
      for (uintptr_t page = start_page; page < start_page + aligned_length; page += PAGE_SIZE_4KB)
      {
        if (unmap_page_in_directory(current_process->page_directory, page) == 0)
          unmapped_pages++;
      }

      /* Free the mapping structure */
      kfree(current);
      KTM_CHECKPOINT(KTM_CP_MM_UNMAP);
      paging_ir0_mm_checkpoint("munmap-after", (int32_t)current_process->task.pid);
      fase39_dump_current_vmas("munmap");
      return 0;
    }
    prev = current;
    current = current->next;
  }

  return -EINVAL; /* Not found */
}

int sys_mprotect(void *addr, size_t len, int prot)
{
  struct mmap_region *current;
  uintptr_t range_start;
  uintptr_t range_end;
  uint64_t *pml4;
  uint64_t map_flags;

  if (!current_process)
    return -ESRCH;
  if (!addr || len == 0)
    return -EINVAL;

  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return -EINVAL;

  if (!is_user_address(addr, len))
    return -EFAULT;

  /* Find the mapping */
  current = current_process->mmap_list;
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
      current->prot = prot;

      range_start = (uintptr_t)addr & (uintptr_t)PAGE_FRAME_MASK;
      range_end = (((uintptr_t)addr + len) + PAGE_SIZE_4KB - 1) & (uintptr_t)PAGE_FRAME_MASK;
      pml4 = current_process->page_directory;

      /*
       * map_page_in_directory() sets PTE present bit and applies PAGE_NX
       * when PAGE_EXEC is absent (matches PAGE_NX if !(prot & PROT_EXEC)).
       */
      map_flags = PAGE_USER;
      if (prot & PROT_WRITE)
        map_flags |= PAGE_RW;
      if (prot & PROT_EXEC)
        map_flags |= PAGE_EXEC;

      for (uintptr_t page = range_start; page < range_end; page += PAGE_SIZE_4KB)
      {
        uint64_t *pte;
        uint64_t phys;

        pte = paging_get_pte(pml4, page);
        if (!pte || !(*pte & PAGE_PRESENT))
        {
          phys = pmm_alloc_frame();
          if (phys == 0)
            return -ENOMEM;
          if (map_page_in_directory(pml4, page, phys, map_flags) != 0)
          {
            pmm_free_frame(phys);
            return -ENOMEM;
          }
          {
            uint64_t old_cr3 = get_current_page_directory();

            load_page_directory((uint64_t)pml4);
            memset((void *)page, 0, PAGE_SIZE_4KB);
            load_page_directory(old_cr3);
          }
          arch_tlb_invalidate_page((uintptr_t)page);
          continue;
        }

        phys = *pte & PAGE_FRAME_MASK;
        if (map_page_in_directory(pml4, page, phys, map_flags) != 0)
          return -ENOMEM;

        /*
         * map_page_in_directory() intentionally skips TLB invalidate (foreign
         * address spaces under kernel root). mprotect changes permissions of an
         * EXISTING mapping in the CURRENT process, so flush the local TLB entry.
         */
        arch_tlb_invalidate_page((uintptr_t)page);
      }

      return 0;
    }
    current = current->next;
  }

  return -EINVAL; /* Not found */
}
