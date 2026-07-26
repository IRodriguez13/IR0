/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fd_types.h
 * Description: Per-process file descriptor table entry types (facade).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdbool.h>
#include <stdint.h>

#define MAX_FDS_PER_PROCESS 64

typedef struct fd_entry
{
	bool in_use;
	char path[256];
	int flags;       /* Open flags (O_RDONLY, O_WRONLY, O_APPEND, etc.) */
	uint8_t fd_flags; /* FD_CLOEXEC etc. */
	void *vfs_file;
	uint64_t offset; /* File offset for seek operations */
	bool is_pipe;  /* 1 if this fd is a pipe */
	int pipe_end;  /* 0 = read end, 1 = write end */
	bool is_devfs; /* bound to devfs node when true */
	uint32_t dev_device_id;
	bool is_socket; /* bound to sock_udp when true */
	bool is_pseudo; /* bound to pseudo_fs ops via vfs_file (pseudo_fd_bind_t) */
	bool is_epoll;  /* vfs_file points at epoll_state */
	bool is_memfd;  /* vfs_file points at ir0_memfd */
	bool is_eventfd;
	bool is_timerfd;
} fd_entry_t;
