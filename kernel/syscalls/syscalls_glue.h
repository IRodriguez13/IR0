/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel — syscall module glue (internal, not userspace ABI).
 * Shared helpers between kernel/syscalls.c and kernel/syscalls/fs_syscalls.c.
 */

#pragma once

#include <kernel/process.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/pipe.h>

#define FD_PROC_BASE  1000
#define FD_DEV_BASE   2000
#define FD_SYS_BASE   3000
#define FD_RANGE_SIZE 1000

fd_entry_t *get_process_fd_table(void);
void ensure_devfs_init(void);
int stdio_is_redirected(fd_entry_t *fd_table, int fd);
int pipe_wait(process_t *proc, pipe_t *pipe, int waiting_read);
void fase50b_dump_bytes(const char *label, const void *buf, size_t n);
void fase48_note_fd_created(void);
int validate_userspace_string(const char *str, size_t max_len);
int validate_userspace_buffer(const void *buf, size_t size);

/* Unredirected stdin read path (keyboard wait + copy to user). */
int64_t syscalls_read_stdio_stdin(void *buf, size_t count);
