/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: map_sys.c
 * Description: mmap(2), munmap(2) and mprotect(2) for anonymous, framebuffer, memfd and private file mappings.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <mm/map_sys.h>
#include <mm/va_search.h>
#include <syscalls/mm_syscalls_internal.h>
#include <config.h>
#include <ir0/copy_user.h>
#include <ir0/filemap.h>
#include <ir0/fb.h>
#include <ir0/kmem.h>
#include <ir0/ktm/checkpoint.h>
#include <ir0/ktm/klog.h>
#include <ir0/memfd.h>
#include <ir0/mm.h>
#include <ir0/mman.h>
#include <ir0/open_flags.h>
#include <ir0/paging.h>
#include <ir0/pmm.h>
#include <ir0/process.h>
#include <ir0/tlb_sync.h>
#include <ir0/vfs.h>
#include <string.h>

static int mm_mmap_verify_ptes(address_space_root_t root, uintptr_t virt_addr,
			       size_t len)
{
	size_t i;

	if (!root || len == 0)
		return -EINVAL;

	for (i = 0; i < len; i += PAGE_SIZE_4KB)
	{
		if (is_page_mapped_in_directory(root, virt_addr + i, NULL) != 1)
			return -ENOMEM;
	}
	return 0;
}

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

static void mmap_audit_log_pte(const char *tag, address_space_root_t root,
                               uintptr_t va)
{
  uint64_t pte_flags = 0;
  uint64_t *pte;
  int mapped;

  if (!DEBUG_MMAP_AUDIT)
    return;
  if (!root)
    return;

  mapped = is_page_mapped_in_directory(root, va, &pte_flags);
  pte = paging_get_pte(root, va);

  if (pte && pte_present(*pte))
  {
    klog_debug_fmt("MMAP", "PTE tag=%s va=%llx mapped=%llx present=%llx "
                   "user=%llx rw=%llx nx=%llx pfn=%llx",
                   tag ? tag : "(null)", (unsigned long long)((uint64_t)va),
                   (unsigned long long)((uint64_t)(mapped > 0 ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && pte_present(*pte) ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_USER ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_RW ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && !mm_pte_executable(*pte) ? 1 : 0)),
                   (unsigned long long)((uint64_t)mm_pte_phys(*pte)));
  }
  else
  {
    klog_debug_fmt("MMAP", "PTE tag=%s va=%llx mapped=%llx present=%llx "
                   "user=%llx rw=%llx nx=%llx",
                   tag ? tag : "(null)", (unsigned long long)((uint64_t)va),
                   (unsigned long long)((uint64_t)(mapped > 0 ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && pte_present(*pte) ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_USER ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte_flags & PAGE_RW ? 1 : 0)),
                   (unsigned long long)((uint64_t)(pte && !mm_pte_executable(*pte) ? 1 : 0)));
  }
}

static void mmap_audit_log_args(void *addr, size_t length, int prot, int flags,
                                int fd, off_t offset)
{
  if (!DEBUG_MMAP_AUDIT)
    return;
  if (current_process)
  {
    klog_debug_fmt("MMAP", "ARGS classify=MMAP_ARGS_DECODED pid=%x comm=%s "
                   "addr=%llx length=%llx prot=%llx flags=%llx fd=%llx offset=%llx "
                   "caller_rip=%llx caller_rsp=%llx",
                   (unsigned)(current_process ? (uint32_t)current_process->task.pid : 0),
                   current_process ? current_process->comm : "(none)",
                   (unsigned long long)((uint64_t)(uintptr_t)addr),
                   (unsigned long long)((uint64_t)length),
                   (unsigned long long)((uint64_t)(unsigned int)prot),
                   (unsigned long long)((uint64_t)(unsigned int)flags),
                   (unsigned long long)((uint64_t)(unsigned int)fd),
                   (unsigned long long)((uint64_t)offset),
                   (unsigned long long)(process_syscall_ip(current_process)),
                   (unsigned long long)(process_syscall_sp(current_process)));
  }
  else
  {
    klog_debug_fmt("MMAP", "ARGS classify=MMAP_ARGS_DECODED pid=%x comm=%s "
                   "addr=%llx length=%llx prot=%llx flags=%llx fd=%llx offset=%llx",
                   (unsigned)(current_process ? (uint32_t)current_process->task.pid : 0),
                   current_process ? current_process->comm : "(none)",
                   (unsigned long long)((uint64_t)(uintptr_t)addr),
                   (unsigned long long)((uint64_t)length),
                   (unsigned long long)((uint64_t)(unsigned int)prot),
                   (unsigned long long)((uint64_t)(unsigned int)flags),
                   (unsigned long long)((uint64_t)(unsigned int)fd),
                   (unsigned long long)((uint64_t)offset));
  }

  if ((flags & MAP_SHARED) != 0 && (flags & MAP_PRIVATE) != 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_UNSUPPORTED_FLAGS reason=MAP_SHARED_and_MAP_PRIVATE");
  }
  if (!(flags & MAP_ANONYMOUS) && fd < 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_UNSUPPORTED_FLAGS reason=file_map_without_fd");
  }
}

static void mmap_audit_log_return(const char *stage, void *ret, uintptr_t virt_addr,
                                  size_t length, int vma_inserted,
                                  address_space_root_t root)
{
  size_t pages = (length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
  size_t mapped_pages = 0;
  size_t i;

  if (!DEBUG_MMAP_AUDIT)
    return;

  klog_debug_fmt("MMAP", "RET stage=%s ret=%llx", stage ? stage : "(null)", (unsigned long long)((uint64_t)(uintptr_t)ret));
  if (mmap_audit_ptr_err(ret))
  {
    klog_debug_fmt("KERN", " errno=%llx", (unsigned long long)((uint64_t)(unsigned int)mmap_audit_errno_from_ret(ret)));
  }
  else
  {
    klog_debug_fmt("KERN", " range=[%llx,%llx) pages=%llx", (unsigned long long)((uint64_t)virt_addr), (unsigned long long)((uint64_t)(virt_addr + length)), (unsigned long long)((uint64_t)pages));
  }
  klog_debug_fmt("KERN", " vma_inserted=%llx", (unsigned long long)((uint64_t)(unsigned int)vma_inserted));

  if (mmap_audit_ptr_err(ret) || !root || virt_addr == 0 || length == 0)
    return;

  for (i = 0; i < pages; i++)
  {
    uintptr_t va = virt_addr + i * PAGE_SIZE_4KB;
    if (is_page_mapped_in_directory(root, va, NULL) == 1)
      mapped_pages++;
  }

  klog_debug_fmt("MMAP", "RET pte_mapped_pages=%llx pte_expected_pages=%llx", (unsigned long long)((uint64_t)mapped_pages), (unsigned long long)((uint64_t)pages));

  if (mapped_pages == 0)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_VMA_WITHOUT_PTES");
  }
  else if (mapped_pages < pages)
  {
    klog_debug("MMAP", "CLASSIFY MMAP_RET_UNMAPPED_RANGE");
  }

  mmap_audit_log_pte("first", root, virt_addr);
  if (pages > 1)
    mmap_audit_log_pte("last", root, virt_addr + (pages - 1) * PAGE_SIZE_4KB);

  if (root)
  {
    uint64_t flags_low = 0;

    if (is_page_mapped_in_directory(root, virt_addr, &flags_low) == 1)
    {
      if (!(flags_low & PAGE_USER))
      {
        klog_debug("MMAP", "CLASSIFY MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_USER");
      }
      if (!(flags_low & PAGE_RW))
      {
        klog_debug("MMAP", "CLASSIFY MMAP_PTE_PERMISSION_BAD reason=missing_PAGE_RW");
      }
    }
  }
}

/*
 * Pick the address of a new mapping of @len bytes: MAP_FIXED takes @addr
 * (replacing what is there), a usable hint is honoured, else the top-down
 * search decides. 0 and the address in @out, or -errno.
 */
static int mm_mmap_place(void *addr, size_t len, int flags, uintptr_t *out)
{
	uintptr_t hint = (uintptr_t)addr;

	if (flags & MAP_FIXED)
	{
		if (!addr || (hint & (PAGE_SIZE_4KB - 1)) != 0)
			return -EINVAL;
		if (!mm_user_range_mappable(hint, len))
			return -ENOMEM;
		if (mm_prepare_map_fixed(hint, len) != 0)
			return -ENOMEM;
		*out = hint;
		return 0;
	}
	if (addr && (hint & (PAGE_SIZE_4KB - 1)) == 0 && hint >= USER_MMAP_START &&
	    hint + len >= hint && hint + len <= USER_MMAP_END &&
	    mm_user_range_mappable(hint, len) &&
	    !process_user_va_range_overlaps(current_process, hint, len))
	{
		uintptr_t va;

		for (va = hint; va < hint + len; va += PAGE_SIZE_4KB)
		{
			if (is_page_mapped_in_directory(process_pgd(current_process), va,
							NULL) == 1)
				break;
		}
		if (va == hint + len)
		{
			*out = hint;
			return 0;
		}
	}
	*out = mm_pick_free_va_topdown(current_process,
				       process_pgd(current_process), len);
	return *out ? 0 : -ENOMEM;
}

/*
 * mmap() of a regular file (Linux do_mmap + filemap_fault). The mapping
 * maps the file's page cache, the pages every mapping of it and
 * read()/write() share. MAP_SHARED stores go to those pages. MAP_PRIVATE
 * pages are copy-on-write: the first store takes a private copy that never
 * reaches the file and that later file writes do not change. Pages inside
 * the file are mapped now (the last one zero past EOF); pages past its end
 * stay empty and raise SIGBUS. PROT_NONE pages are mapped inaccessible so
 * mprotect() can open them later.
 */
static void *mm_mmap_file(void *addr, size_t length, int prot, int flags,
			  int fd, off_t offset)
{
	fd_entry_t *tab = get_process_fd_table();
	address_space_root_t root = process_pgd(current_process);
	struct vfs_file *vf;
	struct mmap_region *region;
	stat_t st;
	uint64_t id = 0;
	uint64_t pte_flags = PAGE_USER;
	uintptr_t va = 0;
	size_t len;
	size_t npages;
	size_t i;
	int shared;
	int acc;
	int ret;

	if (offset < 0)
		return SYSCALL_PTR_ERR(EINVAL);
	if (tab[fd].is_pipe || tab[fd].is_devfs || tab[fd].is_socket ||
	    tab[fd].is_pseudo || tab[fd].is_epoll || tab[fd].is_eventfd ||
	    tab[fd].is_timerfd || !tab[fd].vfs_file)
		return SYSCALL_PTR_ERR(ENODEV);
	vf = (struct vfs_file *)tab[fd].vfs_file;
	if (vfs_stat(vf->path, &st) != 0 || !S_ISREG(st.st_mode))
		return SYSCALL_PTR_ERR(ENODEV);
	/*
	 * Linux do_mmap(): read access always; a shared mapping needs write
	 * access for PROT_WRITE, a private one never writes the file.
	 */
	shared = (flags & MAP_TYPE) != MAP_PRIVATE;
	acc = vf->flags & IR0_O_ACCMODE;
	if (acc == IR0_O_WRONLY)
		return SYSCALL_PTR_ERR(EACCES);
	if (shared && (prot & PROT_WRITE) &&
	    (acc != IR0_O_RDWR || (vf->flags & IR0_O_APPEND)))
		return SYSCALL_PTR_ERR(EACCES);

	len = (length + PAGE_SIZE_4KB - 1) & ~(size_t)(PAGE_SIZE_4KB - 1);
	npages = len / PAGE_SIZE_4KB;
	if ((prot & PROT_WRITE) && shared)
		pte_flags |= PAGE_RW;
	else if (prot & PROT_WRITE)
		pte_flags |= PAGE_COW; /* writable once copied */
	if (prot & PROT_EXEC)
		pte_flags |= PAGE_EXEC;
	if (prot == PROT_NONE)
		pte_flags |= PAGE_PROTNONE;

	region = kcalloc(1, sizeof(*region));
	if (!region)
		return SYSCALL_PTR_ERR(ENOMEM);

	/* No reclaim may drop the cache between filling and mapping it. */
	vfs_pagecache_lock();
	ret = filemap_map_file(vf->path, (uint64_t)offset / PAGE_SIZE_4KB, npages,
			       shared && (prot & PROT_WRITE), &id);
	if (ret == 0)
		ret = mm_mmap_place(addr, len, flags, &va);
	for (i = 0; ret == 0 && i < npages; i++)
	{
		uintptr_t phys;

		if (filemap_get_page(id, (uint64_t)offset / PAGE_SIZE_4KB + i,
				     &phys) != 0)
			break; /* past the end of the file */
		if (map_page_in_directory(root, va + i * PAGE_SIZE_4KB, phys,
					  pte_flags) != 0)
		{
			pmm_frame_put(phys);
			ret = -ENOMEM;
		}
	}
	if (ret != 0)
	{
		if (va)
			unmap_user_region_in_directory(root, va, va + i * PAGE_SIZE_4KB);
		vfs_pagecache_unlock();
		kfree(region);
		filemap_reclaim();
		return SYSCALL_PTR_ERR(-ret);
	}
	/* File code mapped executable (ld.so loading a DSO). */
	if (prot & PROT_EXEC)
		paging_sync_icache_user(root, va, len);

	region->addr = (void *)va;
	region->hint_addr = addr;
	region->length = len;
	region->prot = prot;
	region->flags = flags;
	region->file_id = id;
	region->pgoff = (uint64_t)offset / PAGE_SIZE_4KB;
	region->next = process_mmap_list(current_process);
	process_mm_set_mmap_list(current_process, region);
	vfs_pagecache_unlock();
	KTM_CHECKPOINT(KTM_CP_MM_MAP);
	return (void *)va;
}

/*
 * msync(2) (Linux mm/msync.c): write back the shared file mappings in
 * [addr, addr + len). MS_ASYNC writes back too; there is no deferred
 * writeback to schedule. A range with unmapped pages is ENOMEM.
 */
int mm_msync(void *addr, size_t len, int flags)
{
	uintptr_t start = (uintptr_t)addr;
	uintptr_t end;
	uintptr_t va;
	int unmapped = 0;

	if (!current_process)
		return -ESRCH;
	if (flags & ~(MS_ASYNC | MS_INVALIDATE | MS_SYNC))
		return -EINVAL;
	if (start & (PAGE_SIZE_4KB - 1))
		return -EINVAL;
	if ((flags & MS_ASYNC) && (flags & MS_SYNC))
		return -EINVAL;
	if (len > SIZE_MAX - (PAGE_SIZE_4KB - 1))
		return -ENOMEM;
	len = (len + PAGE_SIZE_4KB - 1) & ~(size_t)(PAGE_SIZE_4KB - 1);
	end = start + len;
	if (end < start)
		return -ENOMEM;
	if (end == start)
		return 0;
	if (!mm_user_range_mappable(start, len))
		return -ENOMEM;

	for (va = start; va < end;)
	{
		struct mmap_region *mr = find_vma(current_process, va);

		if (!mr)
		{
			if (!process_user_va_range_overlaps(current_process, va,
							    PAGE_SIZE_4KB) &&
			    is_page_mapped_in_directory(process_pgd(current_process),
							va, NULL) != 1)
				unmapped = 1;
			va += PAGE_SIZE_4KB;
			continue;
		}
		if (mr->file_id && (mr->flags & MAP_SHARED))
			(void)filemap_writeback(mr->file_id);
		va = (uintptr_t)mr->addr + mr->length;
	}
	return unmapped ? -ENOMEM : 0;
}

void *mm_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
  void *ret;
  uintptr_t virt_addr_out = 0;
  size_t aligned_len = 0;
  int vma_inserted = 0;

  mmap_audit_log_args(addr, length, prot, flags, fd, offset);

  if (!current_process)
  {
    klog_debug("KERN", "SERIAL: mmap: no current process\n");
    ret = (void *)(intptr_t)-ESRCH;
    mmap_audit_log_return("no-process", ret, 0, 0, 0, NULL);
    return ret;
  }

  if (length == 0)
  {
    klog_debug("KERN", "SERIAL: mmap: zero length\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("zero-length", ret, 0, 0, 0, process_pgd(current_process));
    return ret;
  }

  /* Linux: a length the address space cannot hold is ENOMEM. */
  if (length > (USER_MMAP_END - USER_MMAP_START) || length > (1UL << 28))
  {
    klog_debug("KERN", "SERIAL: mmap: length exceeds user mmap arena\n");
    ret = SYSCALL_PTR_ERR(ENOMEM);
    mmap_audit_log_return("length-too-large", ret, 0, 0, 0,
                          process_pgd(current_process));
    return ret;
  }

  /* Validate protection flags */
  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
  {
    klog_debug("KERN", "SERIAL: mmap: invalid protection flags\n");
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("bad-prot", ret, 0, 0, 0, process_pgd(current_process));
    return ret;
  }

  /* Linux do_mmap(): the mapping type is MAP_SHARED or MAP_PRIVATE. */
  switch (flags & MAP_TYPE)
  {
  case MAP_SHARED:
  case MAP_PRIVATE:
  case MAP_SHARED_VALIDATE:
    break;
  default:
    ret = SYSCALL_PTR_ERR(EINVAL);
    mmap_audit_log_return("bad-map-type", ret, 0, 0, 0,
                          process_pgd(current_process));
    return ret;
  }

  /* Validate offset alignment for file mappings */
  if (!(flags & MAP_ANONYMOUS))
  {
    if (fd < 0)
    {
      klog_debug("KERN", "SERIAL: mmap: file mapping requires valid fd\n");
      return SYSCALL_PTR_ERR(EBADF);
    }
    
    /* Offset must be page-aligned for file mappings */
    if (offset % PAGE_SIZE_4KB != 0)
    {
      klog_debug("KERN", "SERIAL: mmap: offset must be page-aligned\n");
      return SYSCALL_PTR_ERR(EINVAL);
    }

    /*
     * Linux: reject invalid fds before ENOSYS for unimplemented file-backed
     * mmap. Only real fd_table slots are valid (devfs /dev/fb0 uses is_devfs).
     */
    {
      fd_entry_t *fd_table = get_process_fd_table();
      bool fd_valid_open = (fd >= 0 && fd < ir0_fd_span() &&
                            fd_table && fd_table[fd].in_use);

      if (!fd_valid_open)
      {
        klog_debug("KERN", "SERIAL: mmap: invalid fd for file-backed mmap\n");
        ret = SYSCALL_PTR_ERR(EBADF);
        mmap_audit_log_return("bad-fd", ret, 0, 0, 0,
                              process_pgd(current_process));
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

      if (fd >= 0 && fd < ir0_fd_span() &&
          fd_table && fd_table[fd].in_use && fd_table[fd].is_devfs)
      {
        device_id = fd_table[fd].dev_device_id;
        fb_mmap_devfs = true;
      }

#if CONFIG_ENABLE_VBE
      if (device_id == 15U)
      {
        struct ir0_fb_info fb_info;
        uint64_t fb_phys;
        uint64_t fb_size;

        if (!ir0_fb_get_info(&fb_info))
          return SYSCALL_PTR_ERR(ENODEV);

        fb_phys = fb_info.fb_phys;
        fb_size = fb_info.fb_size;
        if (fb_phys == 0 || fb_size == 0)
          return SYSCALL_PTR_ERR(ENODEV);

        if (offset < 0 || (uint64_t)offset >= fb_size)
          return SYSCALL_PTR_ERR(EINVAL);

        uint64_t off_u = (uint64_t)offset;
        uint64_t rem = fb_size - off_u;
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
          if (!mm_user_range_mappable(hint_addr, map_len))
            return SYSCALL_PTR_ERR(EFAULT);
          int mapped = is_page_mapped_in_directory(process_pgd(current_process), hint_addr, NULL);
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
              int m = is_page_mapped_in_directory(process_pgd(current_process), check, NULL);
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
        
        /* Linux fb_mmap(): scanout memory is mapped write-combining. */
        uint64_t page_flags = PAGE_USER | PAGE_RW | PAGE_WRITECOMBINE;
        for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
        {
          uintptr_t v = virt_addr + off;
          uint64_t p = fb_phys + off_u + off;
          if (map_page_in_directory(process_pgd(current_process), v, p, page_flags) != 0)
          {
            /* Rollback: unmap already mapped pages */
            for (size_t r = 0; r < off; r += PAGE_SIZE_4KB)
              unmap_page_in_directory(process_pgd(current_process), virt_addr + r);
            return SYSCALL_PTR_ERR(ENOMEM);
          }
        }
        
        struct mmap_region *region = kcalloc(1, sizeof(struct mmap_region));
        if (!region)
        {
          for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
            unmap_page_in_directory(process_pgd(current_process), virt_addr + off);
          return SYSCALL_PTR_ERR(ENOMEM);
        }
        region->addr = (void *)virt_addr;
        region->hint_addr = addr;
        region->length = map_len;
        region->prot = prot;
        region->flags = flags;
        region->next = process_mmap_list(current_process);
        process_mm_set_mmap_list(current_process, region);
        KTM_CHECKPOINT(KTM_CP_MM_MAP);
        {
          static int s_fb_mmap_devfs_tag;
          static int s_fb_mmap_ok_tag;

          if (fb_mmap_devfs && !s_fb_mmap_devfs_tag)
          {
            s_fb_mmap_devfs_tag = 1;
            klog_smoke("FB_MMAP_DEVFS_FD_OK");
            klog_smoke("DEVFB0_MMAP_REAL_FD_OK");
          }
          if (!s_fb_mmap_ok_tag)
          {
            s_fb_mmap_ok_tag = 1;
            klog_smoke("FB_MMAP_OK");
          }
          if ((flags & MAP_SHARED) != 0)
          {
            static int s_fb_map_shared_tag;

            if (!s_fb_map_shared_tag)
            {
              s_fb_map_shared_tag = 1;
              klog_smoke("FB_MAP_SHARED_OK");
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

    /* memfd_create fd: MAP_SHARED (or MAP_PRIVATE copy-like via shared frames). */
    {
      fd_entry_t *fd_table = get_process_fd_table();

      if (fd >= 0 && fd < ir0_fd_span() && fd_table &&
	  fd_table[fd].in_use && fd_table[fd].is_memfd && fd_table[fd].vfs_file)
      {
	struct ir0_memfd *m = (struct ir0_memfd *)fd_table[fd].vfs_file;
	size_t map_len;
	uintptr_t virt_addr = 0;
	uint64_t page_flags;
	int mapped_len;
	struct mmap_region *region;

	if ((flags & MAP_SHARED) == 0 && (flags & MAP_PRIVATE) == 0)
	  return SYSCALL_PTR_ERR(EINVAL);
	if (ir0_memfd_size(m) == 0)
	  return SYSCALL_PTR_ERR(EINVAL);

	map_len = length;
	map_len = (map_len + PAGE_SIZE_4KB - 1) & ~(PAGE_SIZE_4KB - 1);
	if (map_len == 0)
	  return SYSCALL_PTR_ERR(EINVAL);

	if (addr != NULL)
	{
	  uintptr_t hint_addr = (uintptr_t)addr;

	  if ((hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
	    return SYSCALL_PTR_ERR(EINVAL);
	  if (!mm_user_range_mappable(hint_addr, map_len))
	    return SYSCALL_PTR_ERR(EFAULT);
	  if (is_page_mapped_in_directory(process_pgd(current_process),
					  hint_addr, NULL) == 1)
	    return SYSCALL_PTR_ERR(EINVAL);
	  virt_addr = hint_addr;
	}
	else
	{
	  virt_addr = mm_pick_free_va_topdown(current_process,
					      process_pgd(current_process),
					      map_len);
	  if (virt_addr == 0)
	    return SYSCALL_PTR_ERR(ENOMEM);
	}

	page_flags = PAGE_USER;
	if (prot & PROT_WRITE)
	  page_flags |= PAGE_RW;
	mapped_len = ir0_memfd_mmap(m, process_pgd(current_process), virt_addr,
				    map_len, offset, page_flags);
	if (mapped_len < 0)
	  return SYSCALL_PTR_ERR(-mapped_len);
	map_len = (size_t)mapped_len;

	region = kcalloc(1, sizeof(struct mmap_region));
	if (!region)
	{
	  for (size_t off = 0; off < map_len; off += PAGE_SIZE_4KB)
	    unmap_page_in_directory(process_pgd(current_process),
				    virt_addr + off);
	  return SYSCALL_PTR_ERR(ENOMEM);
	}
	region->addr = (void *)virt_addr;
	region->hint_addr = addr;
	region->length = map_len;
	region->prot = prot;
	region->flags = flags;
	region->next = process_mmap_list(current_process);
	process_mm_set_mmap_list(current_process, region);
	{
	  static int s_memfd_map_ok;

	  if (!s_memfd_map_ok)
	  {
	    s_memfd_map_ok = 1;
	    klog_smoke("MEMFD_MAP_SHARED_OK");
	  }
	}
	return (void *)virt_addr;
      }
    }

    return mm_mmap_file(addr, length, prot, flags, fd, offset);
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
    if (addr == NULL || (hint_addr & (PAGE_SIZE_4KB - 1)) != 0)
    {
      ret = SYSCALL_PTR_ERR(EINVAL);
      mmap_audit_log_return("map-fixed-bad-addr", ret, 0, 0, 0,
                            process_pgd(current_process));
      return ret;
    }
    /* Linux: a fixed range outside the user address space is ENOMEM. */
    if (!mm_user_range_mappable(hint_addr, length))
    {
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("map-fixed-kernel-range", ret, hint_addr, length,
			    0, process_pgd(current_process));
      return ret;
    }

    /*
     * MAP_FIXED landing inside the live brk heap: the VMA then shadows
     * pages the break already owns, and a later heap access hits the VMA
     * protection instead of the heap. Narrow condition, logged to show
     * pid and ordering against the break.
     */
    if (hint_addr < (uintptr_t)process_heap_end(current_process) &&
        hint_addr + length > (uintptr_t)process_heap_start(current_process))
      klog_debug_fmt("MMAP",
		      "map-fixed inside brk pid=%x addr=%llx len=%llx prot=%llx heap_end=%llx rip=%llx\n",
		      (unsigned)((uint32_t)current_process->task.pid),
		      (unsigned long long)hint_addr,
		      (unsigned long long)(uint64_t)length,
		      (unsigned long long)(uint64_t)prot,
		      (unsigned long long)(uint64_t)process_heap_end(current_process),
		      (unsigned long long)process_syscall_ip(current_process));

    if (mm_prepare_map_fixed(hint_addr, length) != 0)
    {
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("map-fixed-vma-split-failed", ret, hint_addr,
			    length, 0, process_pgd(current_process));
      return ret;
    }
    virt_addr = hint_addr;
    use_hint = true;
  }
  /*
   * Non-MAP_FIXED: addr is only an advisory hint. If unusable (misaligned,
   * out of range, or already mapped) pick another address — do not EINVAL.
   */
  else if (addr != NULL &&
           (hint_addr & (PAGE_SIZE_4KB - 1)) == 0 &&
           hint_addr >= USER_MMAP_START &&
           hint_addr + length <= USER_MMAP_END &&
           hint_addr + length >= hint_addr &&
           mm_user_range_mappable(hint_addr, length))
  {
    bool collision = false;

    /*
     * A reserved VMA may have no PTEs yet (PROT_NONE, MAP_NORESERVE).
     * Overlap is a collision even when the page tables are empty.
     */
    if (process_user_va_range_overlaps(current_process, hint_addr, length))
      collision = true;

    for (uintptr_t check = hint_addr; check < hint_addr + length;
         check += PAGE_SIZE_4KB)
    {
      if (is_page_mapped_in_directory(process_pgd(current_process), check,
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
                                        process_pgd(current_process),
                                        length);
    if (virt_addr == 0)
      return SYSCALL_PTR_ERR(ENOMEM);
  }

  if ((flags & MAP_FIXED) == 0 &&
      (virt_addr < USER_MMAP_START ||
       virt_addr + aligned_len > USER_MMAP_END ||
       virt_addr + aligned_len < virt_addr))
  {
    klog_debug_fmt("MMAP",
		    "reject va=%llx len=%llx outside mmap arena\n",
		    (unsigned long long)virt_addr,
		    (unsigned long long)aligned_len);
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

  /*
   * Leave anonymous PROT_NONE and MAP_NORESERVE unpopulated. The first
   * user access demand-zeros one page. MAP_NORESERVE does not charge
   * commit, so a mapping larger than free RAM is allowed.
   * Linux: mm/mmap.c do_mmap / accountable_mapping, MAP_NORESERVE.
   */
  if (!((flags & MAP_ANONYMOUS) &&
	(prot == PROT_NONE || (flags & MAP_NORESERVE) != 0)))
  {
    if (map_user_region_in_directory(process_pgd(current_process), virt_addr, length, page_flags) != 0)
    {
      klog_debug("KERN", "SERIAL: mmap: failed to map pages\n");
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("map-failed", ret, virt_addr, aligned_len, 0,
                            process_pgd(current_process));
      return ret;
    }

    if (mm_mmap_verify_ptes(process_pgd(current_process), virt_addr,
                            aligned_len) != 0)
    {
      for (uintptr_t page = virt_addr; page < virt_addr + aligned_len;
           page += PAGE_SIZE_4KB)
        unmap_page_in_directory(process_pgd(current_process), page);
      ret = SYSCALL_PTR_ERR(ENOMEM);
      mmap_audit_log_return("pte-verify-fail", ret, virt_addr, aligned_len, 0,
                            process_pgd(current_process));
      return ret;
    }

    virt_addr_out = virt_addr;
    mmap_audit_log_pte("post-map", process_pgd(current_process), virt_addr);
  }
  else if (DEBUG_MMAP_AUDIT)
  {
    klog_debug_fmt("MMAP", "RESERVE stage=vma-only prot_none anon len=%llx", (unsigned long long)((uint64_t)aligned_len));
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
      klog_debug("MMAP", "ZERO stage=skipped reason=prot_none");
    }
    else if (prot & PROT_WRITE)
    {
      klog_debug("MMAP", "ZERO stage=phys-prezeroed-in-map_user_region");
    }
    else
    {
      klog_debug("MMAP", "ZERO stage=skipped reason=no_prot_write");
    }
  }

  /* Create mapping entry */
  struct mmap_region *region = kcalloc(1, sizeof(struct mmap_region));
  if (!region)
  {
      /* Failed to allocate region entry - unmap pages */
      if (!((flags & MAP_ANONYMOUS) &&
	    (prot == PROT_NONE || (flags & MAP_NORESERVE) != 0)))
      {
        for (uintptr_t page = virt_addr; page < virt_addr + length; page += PAGE_SIZE_4KB)
        {
          unmap_page_in_directory(process_pgd(current_process), page);
        }
      }
    return SYSCALL_PTR_ERR(ENOMEM);
  }

  region->addr = (void *)virt_addr;
  region->hint_addr = addr;  /* Store hint for future reference */
  region->length = length;
  region->prot = prot;  /* Store protection flags for mprotect */
  region->flags = flags;
  region->next = process_mmap_list(current_process);
  process_mm_set_mmap_list(current_process, region);
  vma_inserted = 1;
  virt_addr_out = virt_addr;
  KTM_CHECKPOINT(KTM_CP_MM_MAP);

  ret = (void *)virt_addr;
  mmap_audit_log_return("ok", ret, virt_addr_out, aligned_len, vma_inserted,
                        process_pgd(current_process));
  if (DEBUG_MMAP_AUDIT)
    klog_debug("MMAP", "CLASSIFY MMAP_RETURN_OK stage=mmap-return-ok");
  return ret;
}

int mm_munmap(void *addr, size_t length)
{
  uintptr_t start_page;
  uintptr_t end_page;
  size_t aligned_length;
  int detached;

  if (!current_process)
    return -ESRCH;
  /* Linux __vm_munmap(): the address must be page-aligned. */
  if (!addr || length == 0 || ((uintptr_t)addr & (PAGE_SIZE_4KB - 1)) != 0)
    return -EINVAL;

  if (length > SIZE_MAX - (PAGE_SIZE_4KB - 1))
    return -EINVAL;

  /* Round the length up to whole pages. */
  start_page = (uintptr_t)addr & (uintptr_t)PAGE_FRAME_MASK;
  aligned_length = (length + PAGE_SIZE_4KB - 1) & (size_t)PAGE_FRAME_MASK;
  if (start_page > UINTPTR_MAX - aligned_length)
    return -EINVAL;
  end_page = start_page + aligned_length;

  /* Linux: munmap outside the user address space is EINVAL. */
  if (!mm_user_range_mappable(start_page, aligned_length))
    return -EINVAL;

  paging_ir0_mm_checkpoint("munmap-before", (int32_t)process_get_pid());

  /*
   * Drop only the requested range. A partial munmap must split the VMA:
   * freeing the whole record left the surviving PTEs untracked, so their
   * frames could never be released through munmap again.
   */
  detached = mm_detach_vma_range(start_page, end_page);
  if (detached < 0)
    return detached;

  /*
   * Linux munmap(2): any page of the user range goes, VMA or not (ELF
   * image pages are not in the mmap list), and a range with no mapping
   * at all is not an error.
   */
  unmap_user_region_in_directory(process_pgd(current_process), start_page,
                                 end_page);

  /* A page cache no mapping uses any more goes back to its file. */
  filemap_reclaim();

  KTM_CHECKPOINT(KTM_CP_MM_UNMAP);
  paging_ir0_mm_checkpoint("munmap-after",
			   (int32_t)current_process->task.pid);
  return 0;
}

int mm_mprotect(void *addr, size_t len, int prot)
{
  struct mmap_region *current;
  struct mmap_region *matched = NULL;
  uintptr_t range_start;
  uintptr_t range_end;
  address_space_root_t root;
  uint64_t map_flags;
  int saw_present = 0;

  if (!current_process)
    return -ESRCH;
  if (!addr || len == 0)
    return -EINVAL;

  if (((uintptr_t)addr & (PAGE_SIZE_4KB - 1)) != 0)
    return -EINVAL;

  if ((prot & ~(PROT_READ | PROT_WRITE | PROT_EXEC)) != 0)
    return -EINVAL;

  /*
   * Linux allows mprotect on any mapped VA (including ELF LOAD segments).
   * IR0 mmap_list only tracks sys_mmap regions — fall through to PTE walk so
   * libc RELRO (mprotect → PROT_READ after load) works for exec images.
   */
  current = process_mmap_list(current_process);
  while (current)
  {
    if (current->addr <= addr &&
        (char *)addr + len <= (char *)current->addr + current->length)
    {
      matched = current;
      break;
    }
    current = current->next;
  }

  range_start = (uintptr_t)addr & (uintptr_t)PAGE_FRAME_MASK;
  if (len > UINTPTR_MAX - (uintptr_t)addr)
    return -EINVAL;
  range_end = (((uintptr_t)addr + len) + PAGE_SIZE_4KB - 1) &
	      (uintptr_t)PAGE_FRAME_MASK;
  root = process_pgd(current_process);

  /* Linux: mprotect outside the user address space is ENOMEM. */
  if (!mm_user_range_mappable(range_start, range_end - range_start))
    return -ENOMEM;

  map_flags = PAGE_USER;
  if (prot & PROT_WRITE)
    map_flags |= PAGE_RW;
  if (prot & PROT_EXEC)
    map_flags |= PAGE_EXEC;
  /* Keep the frame, deny every user access (Linux PAGE_NONE). */
  if (prot == PROT_NONE)
    map_flags |= PAGE_PROTNONE;

  for (uintptr_t page = range_start; page < range_end; page += PAGE_SIZE_4KB)
  {
    uint64_t *pte;
    uint64_t phys;
    uint64_t page_flags = map_flags;

    pte = paging_get_pte(root, page);
    if (!pte || !pte_present(*pte))
    {
      /*
       * A known VMA with no page yet stays empty. PROT_NONE→RW only
       * updates the VMA; the fault allocates the page that is touched.
       * A hole with no VMA does not count as a mapping.
       */
      if (matched)
        saw_present = 1;
      continue;
    }

    saw_present = 1;
    /*
     * PAGE_COW means "writable once copied": without PROT_WRITE a store
     * must fault and uaccess must fail, not copy the page. Take the private
     * copy now so the page stays exclusive if PROT_WRITE comes back.
     */
    if (!(prot & PROT_WRITE) && pte_cow(*pte))
    {
      if (paging_cow_unshare(root, page, 0) != 0)
        return -ENOMEM;
      pte = paging_get_pte(root, page);
      if (!pte || !pte_present(*pte))
        return -ENOMEM;
    }
    phys = mm_pte_phys(*pte);
    /*
     * Keep the software COW bit and the hardware access bits. A shared
     * frame must stay read-only even when mprotect asks for PROT_WRITE;
     * clearing PAGE_COW here would make the next store write through the
     * parent's frame instead of copying it.
     */
    page_flags |= mm_pte_mapping_flags(*pte) & (PAGE_COW | PAGE_DIRTY | PAGE_ACCESSED);
    /*
     * A frame fork() shares read-only (image text, RELRO, .rodata) still
     * belongs to the other process too: PROT_WRITE makes it copy-on-write,
     * never writable in place (Linux do_wp_page). MAP_SHARED pages are
     * shared on purpose and stay writable.
     */
    if ((page_flags & PAGE_RW) && pmm_frame_refcount(phys) > 1 &&
        !vma_is_shared(current_process, page))
      page_flags |= PAGE_COW;
    if ((page_flags & PAGE_COW) && (page_flags & PAGE_RW))
      page_flags &= ~PAGE_RW;
    if (map_page_in_directory(root, page, phys, page_flags) != 0)
      return -ENOMEM;
    mm_tlb_sync_page(root, (uintptr_t)page);
  }

  if (!saw_present)
    return -ENOMEM; /* Linux: no mapping in range → typically ENOMEM */

  if (mm_apply_vma_prot(range_start, range_end, prot) != 0)
    return -ENOMEM;

  return 0;
}
