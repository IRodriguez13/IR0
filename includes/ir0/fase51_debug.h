/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — FASE51 shell bring-up serial diagnostics (gated).
 */

#ifndef _IR0_FASE51_DEBUG_H
#define _IR0_FASE51_DEBUG_H

#include <config.h>
#include <stddef.h>
#include <stdint.h>

#if CONFIG_DEBUG_FASE51
#define IR0_FASE51_DBG 1
#else
#define IR0_FASE51_DBG 0
#endif

void fase51_dbg_pipe2(int read_fd, int write_fd, int flags, int64_t ret);
void fase51_dbg_dup2(int oldfd, int newfd, int64_t ret);
void fase51_dbg_close(int fd, int is_pipe, int64_t ret);
void fase51_dbg_wait4(int64_t pid, int64_t ret);
void fase51_dbg_pipe_rw(const char *op, int fd, int64_t ret);
void fase51_dbg_open_redirect(const char *path, int linux_flags, int64_t ret);
void fase51_dbg_exec_argv(const char *path, const char *argv0, const char *argv1);
void fase51_dbg_wait_wake(uint32_t parent_pid, uint32_t child_pid, int *status_ptr,
			  int status_val, int copy_ret);

#endif /* _IR0_FASE51_DEBUG_H */
