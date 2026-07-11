/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.c
 * Description: IPC pipes — FASE49 FD/pipe lifetime + EOF/EPIPE semantics
 */

#include "pipe.h"
#include <ir0/kmem.h>
#include <ir0/errno.h>
#include <string.h>
#include <config.h>
static uint64_t fase48_pipe_created;
static uint64_t fase48_pipe_destroyed;
static uint64_t fase49_next_pipe_id = 1;

static void fase49_pipe_line(pipe_t *pipe, const char *event)
{
	if (!pipe || !event)
		return;

}

void pipe_fase49_fd_trace(uint32_t pid, int fd, pipe_t *pipe, int end,
			  int refcount, const char *op)
{
	(void)pid;
	(void)fd;
	(void)pipe;
	(void)end;
	(void)refcount;
	(void)op;
}

void pipe_fase48_get_stats(uint64_t *created, uint64_t *destroyed)
{
	if (created)
		*created = fase48_pipe_created;
	if (destroyed)
		*destroyed = fase48_pipe_destroyed;
}

static void pipe_try_free(pipe_t *pipe, const char *event)
{
	if (!pipe)
		return;

	if (pipe->fd_refs <= 0)
	{
		fase49_pipe_line(pipe, event ? event : "DESTROY");
		fase48_pipe_destroyed++;
		kfree(pipe);
	}
}

pipe_t *pipe_create(void)
{
	pipe_t *pipe = kmalloc_try(sizeof(pipe_t));

	if (!pipe)
		return NULL;

	memset(pipe, 0, sizeof(*pipe));
	pipe->pipe_id = fase49_next_pipe_id++;
	fase48_pipe_created++;
	fase49_pipe_line(pipe, "CREATE");
	return pipe;
}

/*
 * Register one open fd slot on @end (0 read, 1 write).
 * Called from pipe2 install, dup/dup2, and fork inheritance.
 */
void pipe_acquire_end(pipe_t *pipe, int end)
{
	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	pipe->fd_refs++;
	if (end == 0)
		pipe->readers++;
	else
		pipe->writers++;

	fase49_pipe_line(pipe, "ACQUIRE");
}

void pipe_acquire(pipe_t *pipe)
{
	if (!pipe)
		return;

	pipe->fd_refs++;
	fase49_pipe_line(pipe, "ACQUIRE");
}

int pipe_read(pipe_t *pipe, void *buf, size_t count)
{
	if (!pipe || !buf)
		return -EINVAL;

	if (pipe->count == 0)
	{
		if (pipe->writers <= 0)
		{
			fase49_pipe_line(pipe, "EOF");
			return 0;
		}
		return -EAGAIN;
	}

	size_t to_read = (count < pipe->count) ? count : pipe->count;
	char *dest = (char *)buf;
	size_t bytes_read = 0;

	while (bytes_read < to_read)
	{
		dest[bytes_read] = pipe->buffer[pipe->read_pos];
		pipe->read_pos = (pipe->read_pos + 1) % PIPE_SIZE;
		bytes_read++;
	}

	pipe->count -= bytes_read;
	return (int)bytes_read;
}

int pipe_write(pipe_t *pipe, const void *buf, size_t count)
{
	if (!pipe || !buf)
		return -EINVAL;

	if (pipe->readers <= 0)
		return -EPIPE;

	if (pipe->count >= PIPE_SIZE)
		return -EAGAIN;

	size_t space = PIPE_SIZE - pipe->count;
	size_t to_write = (count < space) ? count : space;
	const char *src = (const char *)buf;
	size_t bytes_written = 0;

	while (bytes_written < to_write)
	{
		pipe->buffer[pipe->write_pos] = src[bytes_written];
		pipe->write_pos = (pipe->write_pos + 1) % PIPE_SIZE;
		bytes_written++;
	}

	pipe->count += bytes_written;

	if (bytes_written > 0)
	{
	}

	return (int)bytes_written;
}

void pipe_close_end(pipe_t *pipe, int end)
{
	if (!pipe)
		return;

	if (end != 0 && end != 1)
		return;

	if (end == 0)
	{
		if (pipe->readers > 0)
			pipe->readers--;
		if (!pipe->closed_read)
			pipe->closed_read = 1;
	}
	else
	{
		if (pipe->writers > 0)
			pipe->writers--;
		if (!pipe->closed_write)
			pipe->closed_write = 1;
	}

	if (pipe->fd_refs > 0)
		pipe->fd_refs--;

	fase49_pipe_line(pipe, "CLOSE");
	pipe_try_free(pipe, "DESTROY");
}

void pipe_fase49_note_read_sleep(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "READ_SLEEP");
}

void pipe_fase49_note_read_wake(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "READ_WAKE");
}

void pipe_fase49_note_write_wake(pipe_t *pipe)
{
	fase49_pipe_line(pipe, "WRITE_WAKE");
}

void pipe_abort_unopened(pipe_t *pipe)
{
	if (!pipe)
		return;

	fase49_pipe_line(pipe, "DESTROY");
	fase48_pipe_destroyed++;
	kfree(pipe);
}

extern void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
				uint64_t *blocked_readers, uint64_t *blocked_writers);

void pipe_fase49_classify(void)
{
	uint64_t created = 0;
	uint64_t destroyed = 0;
	uint64_t fd_created = 0;
	uint64_t fd_destroyed = 0;
	uint64_t blocked_readers = 0;
	uint64_t blocked_writers = 0;
	const char *cls;

	pipe_fase48_get_stats(&created, &destroyed);
	fase48_fd_get_stats(&fd_created, &fd_destroyed, &blocked_readers,
			    &blocked_writers);

	if (created == destroyed)
		cls = "PIPE_READY";
	else
		cls = "PIPE_REF_LEAK";
	(void)cls;
	(void)fd_created;
	(void)fd_destroyed;
	(void)blocked_readers;
	(void)blocked_writers;
}
