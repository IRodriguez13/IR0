/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pipe_fd.h
 * Description: Pipe/fifo fd table helpers (O_RDWR named FIFO)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/fcntl.h>
#include <ir0/fd_types.h>
#include <ir0/pipe.h>

/*
 * Linux fifo(7): O_RDWR on a named FIFO holds both ends on one fd — empty pipe
 * is not EOF-readable and select(2) must not spin on POLLIN.
 */
static inline int fd_entry_pipe_is_rdwr(const fd_entry_t *e)
{
	return e && e->is_pipe && e->pipe_end == 0 &&
	       (e->flags & O_ACCMODE) == O_RDWR;
}

static inline void pipe_fd_entry_acquire_refs(const fd_entry_t *e)
{
	pipe_t *pipe;

	if (!e || !e->is_pipe || !e->vfs_file)
		return;

	pipe = (pipe_t *)e->vfs_file;
	pipe_acquire_end(pipe, e->pipe_end);
	if (fd_entry_pipe_is_rdwr(e))
		pipe_acquire_end(pipe, 1);
}

static inline void pipe_fd_entry_release_refs(pipe_t *pipe, const fd_entry_t *e)
{
	if (!pipe || !e)
		return;

	if (fd_entry_pipe_is_rdwr(e))
		pipe_close_end(pipe, 1);
	pipe_close_end(pipe, e->pipe_end);
}

static inline int fd_entry_pipe_can_write(const fd_entry_t *e)
{
	pipe_t *pipe;

	if (!e || !e->is_pipe || !e->vfs_file)
		return 0;

	pipe = (pipe_t *)e->vfs_file;
	if (e->pipe_end == 1)
		return (pipe->readers > 0 && pipe->count < PIPE_SIZE) ? 1 : 0;
	if (fd_entry_pipe_is_rdwr(e))
		return (pipe->readers > 0 && pipe->count < PIPE_SIZE) ? 1 : 0;
	return 0;
}

static inline int fd_entry_pipe_can_read(const fd_entry_t *e)
{
	pipe_t *pipe;

	if (!e || !e->is_pipe || !e->vfs_file || e->pipe_end != 0)
		return 0;

	pipe = (pipe_t *)e->vfs_file;
	if (pipe->count > 0)
		return 1;
	if (fd_entry_pipe_is_rdwr(e))
		return 0;
	return (pipe->writers <= 0) ? 1 : 0;
}
