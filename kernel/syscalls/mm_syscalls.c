/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: mm_syscalls.c
 * Description: mmap, munmap and mprotect wrappers and sysinfo.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "mm_syscalls.h"
#include <mm/map_sys.h>
#include <ir0/clock.h>
#include <ir0/copy_user.h>
#include <ir0/mm_port.h>
#include <ir0/swap.h>
#include <string.h>

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

int sys_msync(void *addr, size_t len, int flags)
{
	return mm_msync(addr, len, flags);
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

