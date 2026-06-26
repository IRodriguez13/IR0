/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fase52_debug.c
 * Description: FASE52 gated serial diagnostics for TCC bring-up paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "fase52_debug.h"
#include <ir0/serial_io.h>
#include <ir0/process.h>

static void fase52_print_pid(void)
{
	serial_print("[FASE52] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
}

void fase52_dbg_brk(void *addr, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" brk addr=");
	serial_print_hex64((uint64_t)(uintptr_t)addr);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)addr;
	(void)ret;
#endif
}

void fase52_dbg_mmap(void *addr, size_t len, int prot, int flags, int fd, int64_t off,
		     void *ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" mmap addr=");
	serial_print_hex64((uint64_t)(uintptr_t)addr);
	serial_print(" len=");
	serial_print_hex64((uint64_t)len);
	serial_print(" prot=");
	serial_print_hex64((uint64_t)(unsigned int)prot);
	serial_print(" flags=");
	serial_print_hex64((uint64_t)(unsigned int)flags);
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" off=");
	serial_print_hex64((uint64_t)off);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)(uintptr_t)ret);
	serial_print("\n");
#else
	(void)addr;
	(void)len;
	(void)prot;
	(void)flags;
	(void)fd;
	(void)off;
	(void)ret;
#endif
}

void fase52_dbg_munmap(void *addr, size_t len, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" munmap addr=");
	serial_print_hex64((uint64_t)(uintptr_t)addr);
	serial_print(" len=");
	serial_print_hex64((uint64_t)len);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)addr;
	(void)len;
	(void)ret;
#endif
}

void fase52_dbg_mprotect(void *addr, size_t len, int prot, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" mprotect addr=");
	serial_print_hex64((uint64_t)(uintptr_t)addr);
	serial_print(" len=");
	serial_print_hex64((uint64_t)len);
	serial_print(" prot=");
	serial_print_hex64((uint64_t)(unsigned int)prot);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)addr;
	(void)len;
	(void)prot;
	(void)ret;
#endif
}

void fase52_dbg_openat(int dfd, const char *path, int flags, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" openat dfd=");
	serial_print_hex64((uint64_t)dfd);
	serial_print(" path=");
	serial_print(path ? path : "(null)");
	serial_print(" flags=0x");
	serial_print_hex64((uint64_t)(unsigned int)flags);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)dfd;
	(void)path;
	(void)flags;
	(void)ret;
#endif
}

void fase52_dbg_rw(const char *op, int fd, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" ");
	serial_print(op ? op : "rw");
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)op;
	(void)fd;
	(void)ret;
#endif
}

void fase52_dbg_lseek(int fd, int64_t off, int whence, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" lseek fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" off=");
	serial_print_hex64((uint64_t)off);
	serial_print(" whence=");
	serial_print_hex64((uint64_t)(unsigned int)whence);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)fd;
	(void)off;
	(void)whence;
	(void)ret;
#endif
}

void fase52_dbg_stat_path(const char *path, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" stat path=");
	serial_print(path ? path : "(null)");
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)path;
	(void)ret;
#endif
}

void fase52_dbg_access(const char *path, int mode, int64_t ret)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" access path=");
	serial_print(path ? path : "(null)");
	serial_print(" mode=");
	serial_print_hex64((uint64_t)(unsigned int)mode);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)path;
	(void)mode;
	(void)ret;
#endif
}

void fase52_dbg_exec_argv(const char *path, const char *argv0, const char *argv1,
			  uint64_t env_count)
{
#if CONFIG_DEBUG_FASE52
	fase52_print_pid();
	serial_print(" exec path=");
	serial_print(path ? path : "(null)");
	serial_print(" argv0=");
	serial_print(argv0 ? argv0 : "(null)");
	serial_print(" argv1=");
	serial_print(argv1 ? argv1 : "(null)");
	serial_print(" env_n=");
	serial_print_hex64(env_count);
	serial_print("\n");
#else
	(void)path;
	(void)argv0;
	(void)argv1;
	(void)env_count;
#endif
}
