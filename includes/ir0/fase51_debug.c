/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fase51_debug.c
 * Description: FASE51 gated serial diagnostics for shell/pipe/redir paths
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "fase51_debug.h"
#include <ir0/serial_io.h>
#include <ir0/process.h>

void fase51_dbg_pipe2(int read_fd, int write_fd, int flags, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][PIPE2] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" rfd=");
	serial_print_hex64((uint64_t)read_fd);
	serial_print(" wfd=");
	serial_print_hex64((uint64_t)write_fd);
	serial_print(" flags=");
	serial_print_hex64((uint64_t)(uint32_t)flags);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)read_fd;
	(void)write_fd;
	(void)flags;
	(void)ret;
#endif
}

void fase51_dbg_dup2(int oldfd, int newfd, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][DUP2] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" old=");
	serial_print_hex64((uint64_t)oldfd);
	serial_print(" new=");
	serial_print_hex64((uint64_t)newfd);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)oldfd;
	(void)newfd;
	(void)ret;
#endif
}

void fase51_dbg_close(int fd, int is_pipe, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][CLOSE] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" pipe=");
	serial_print_hex64((uint64_t)(unsigned int)is_pipe);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)fd;
	(void)is_pipe;
	(void)ret;
#endif
}

void fase51_dbg_wait4(int64_t pid, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][WAIT4] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" wait=");
	serial_print_hex64((uint64_t)pid);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)pid;
	(void)ret;
#endif
}

void fase51_dbg_pipe_rw(const char *op, int fd, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][PIPE_RW] op=");
	serial_print(op ? op : "?");
	serial_print(" pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
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

void fase51_dbg_open_redirect(const char *path, int linux_flags, int64_t ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][OPEN_REDIR] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" path=");
	serial_print(path ? path : "(null)");
	serial_print(" flags=0x");
	serial_print_hex64((uint64_t)(uint32_t)linux_flags);
	serial_print(" ret=");
	serial_print_hex64((uint64_t)ret);
	serial_print("\n");
#else
	(void)path;
	(void)linux_flags;
	(void)ret;
#endif
}

void fase51_dbg_exec_argv(const char *path, const char *argv0, const char *argv1)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][EXEC] pid=");
	serial_print_hex32(current_process ?
			   (uint32_t)current_process->task.pid : 0);
	serial_print(" path=");
	serial_print(path ? path : "(null)");
	serial_print(" argv0=");
	serial_print(argv0 ? argv0 : "(null)");
	serial_print(" argv1=");
	serial_print(argv1 ? argv1 : "(null)");
	serial_print("\n");
#else
	(void)path;
	(void)argv0;
	(void)argv1;
#endif
}

void fase51_dbg_wait_wake(uint32_t parent_pid, uint32_t child_pid, int *status_ptr,
			  int status_val, int copy_ret)
{
#if CONFIG_DEBUG_FASE51
	serial_print("[FASE51][WAIT_WAKE] parent=");
	serial_print_hex32(parent_pid);
	serial_print(" child=");
	serial_print_hex32(child_pid);
	serial_print(" status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)status_ptr);
	serial_print(" status_val=");
	serial_print_hex64((uint64_t)(uint32_t)status_val);
	serial_print(" copy_ret=");
	serial_print_hex64((uint64_t)(int64_t)copy_ret);
	serial_print("\n");
#else
	(void)parent_pid;
	(void)child_pid;
	(void)status_ptr;
	(void)status_val;
	(void)copy_ret;
#endif
}
