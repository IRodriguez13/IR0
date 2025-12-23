// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.c
 * Description: Basic IPC pipes implementation
 */

#include "pipe.h"
#include <ir0/memory/kmem.h>
#include <string.h>
#include <drivers/serial/serial.h>
#include <config.h>

/**
 * pipe_create - Create new pipe
 */
pipe_t *pipe_create(void)
{
    pipe_t *pipe = kmalloc(sizeof(pipe_t));
    if (!pipe)
    {
        return NULL;
    }

    /* Initialize pipe */
    pipe->read_pos = 0;
    pipe->write_pos = 0;
    pipe->count = 0;
    pipe->ref_count = 2; /* Both read and write ends open */

#if DEBUG_PROCESS
    serial_print("[PIPE] Created new pipe\n");
#endif

    return pipe;
}

/**
 * pipe_read - Read from pipe (circular buffer)
 */
int pipe_read(pipe_t *pipe, void *buf, size_t count)
{
    if (!pipe || !buf)
    {
        return -1;
    }

    /* No data available */
    if (pipe->count == 0)
    {
        return 0;
    }

    /* Read what's available */
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

#if DEBUG_PROCESS
    serial_print("[PIPE] Read from pipe\n");
#endif

    return bytes_read;
}

/**
 * pipe_write - Write to pipe (circular buffer)
 */
int pipe_write(pipe_t *pipe, const void *buf, size_t count)
{
    if (!pipe || !buf)
    {
        return -1;
    }

    /* Pipe full */
    if (pipe->count >= PIPE_SIZE)
    {
        return -1;
    }

    /* Write what fits */
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

#if DEBUG_PROCESS
    serial_print("[PIPE] Written to pipe\n");
#endif

    return bytes_written;
}

/**
 * pipe_close - Close one end of pipe
 */
void pipe_close(pipe_t *pipe)
{
    if (!pipe)
    {
        return;
    }

    pipe->ref_count--;

    /* Both ends closed - free pipe */
    if (pipe->ref_count <= 0)
    {
#if DEBUG_PROCESS
        serial_print("[PIPE] Destroying pipe\n");
#endif
        kfree(pipe);
    }
}
