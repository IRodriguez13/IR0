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
#include <ir0/serial_io.h>
#include <config.h>
#include <ir0/fase50_debug.h>

static uint64_t fase48_pipe_created;
static uint64_t fase48_pipe_destroyed;
static uint64_t fase49_next_pipe_id = 1;

static void fase49_pipe_line(pipe_t *pipe, const char *event)
{
	if (!pipe || !event)
		return;

	serial_print("[FASE49][PIPE] event=");
	serial_print(event);
	serial_print(" pipe_id=");
	serial_print_hex64(pipe->pipe_id);
	serial_print(" readers=");
	serial_print_hex64((uint64_t)pipe->readers);
	serial_print(" writers=");
	serial_print_hex64((uint64_t)pipe->writers);
	serial_print(" buffer_bytes=");
	serial_print_hex64((uint64_t)pipe->count);
	serial_print(" closed_read=");
	serial_print(pipe->closed_read ? "1" : "0");
	serial_print(" closed_write=");
	serial_print(pipe->closed_write ? "1" : "0");
	serial_print(" fd_refs=");
	serial_print_hex64((uint64_t)pipe->fd_refs);
	serial_print("\n");
}

void pipe_fase49_fd_trace(uint32_t pid, int fd, pipe_t *pipe, int end,
			  int refcount, const char *op)
{
	serial_print("[FASE49][FD] op=");
	serial_print(op ? op : "(null)");
	serial_print(" pid=");
	serial_print_hex32(pid);
	serial_print(" fd=");
	serial_print_hex64((uint64_t)fd);
	serial_print(" object=");
	if (pipe)
		serial_print_hex64(pipe->pipe_id);
	else
		serial_print("0");
	serial_print(" end=");
	serial_print_hex64((uint64_t)end);
	serial_print(" refcount=");
	serial_print_hex64((uint64_t)refcount);
	serial_print("\n");
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
#if CONFIG_DEBUG_FASE50
		serial_print("[FASE50B][PIPE_WRITE] pipe_id=");
		serial_print_hex64(pipe->pipe_id);
		serial_print(" bytes=");
		serial_print_hex64((uint64_t)bytes_written);
		serial_print(" buffer_bytes=");
		serial_print_hex64((uint64_t)pipe->count);
		serial_print(" hex=");
		{
			size_t di;

			for (di = 0; di < bytes_written && di < 32; di++)
			{
				if (di > 0)
					serial_print(" ");
				serial_print_hex64((uint64_t)(uint8_t)src[di]);
			}
		}
		serial_print(" ascii=");
		{
			size_t di;

			for (di = 0; di < bytes_written && di < 32; di++)
			{
				char c = src[di];

				if (c >= 32 && c <= 126)
					serial_putchar(c);
				else
					serial_print(".");
			}
		}
		serial_print("\n");
		if (bytes_written == 6 && src[0] == 'h' && src[1] == 'e')
			serial_print("[FASE50B][CLASSIFY] PIPE_WRITE_CONTENT_OK\n");
#endif
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

	serial_print("[FASE49][CLASS] pipe_created=");
	serial_print_hex64(created);
	serial_print(" pipe_destroyed=");
	serial_print_hex64(destroyed);
	serial_print(" fd_created=");
	serial_print_hex64(fd_created);
	serial_print(" fd_destroyed=");
	serial_print_hex64(fd_destroyed);
	serial_print(" blocked_readers=");
	serial_print_hex64(blocked_readers);
	serial_print(" blocked_writers=");
	serial_print_hex64(blocked_writers);
	serial_print(" pipe_class=");
	serial_print(cls);
	serial_print("\n");
}
