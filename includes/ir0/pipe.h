// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: pipe.h
 * Description: Basic IPC pipes implementation
 */

#ifndef _IR0_PIPE_H
#define _IR0_PIPE_H

#include <stdint.h>
#include <stddef.h>

/* ========================================================================== */
/* PIPE CONSTANTS                                                            */
/* ========================================================================== */

#define PIPE_SIZE 4096  /* 4KB pipe buffer */

/* ========================================================================== */
/* PIPE STRUCTURE                                                            */
/* ========================================================================== */

typedef struct pipe
{
    char buffer[PIPE_SIZE];
    size_t read_pos;
    size_t write_pos;
    size_t count;          /* Number of bytes in buffer */
    int ref_count;         /* Number of open file descriptors */
} pipe_t;

/* ========================================================================== */
/* FUNCTION DECLARATIONS                                                     */
/* ========================================================================== */

/**
 * pipe_create - Create a new pipe
 *
 * Returns: Pointer to new pipe, or NULL on failure
 */
pipe_t *pipe_create(void);

/**
 * pipe_read - Read from pipe
 * @pipe: Pipe to read from
 * @buf: Buffer to read into
 * @count: Maximum bytes to read
 *
 * Returns: Number of bytes read, 0 if empty, -1 on error
 */
int pipe_read(pipe_t *pipe, void *buf, size_t count);

/**
 * pipe_write - Write to pipe
 * @pipe: Pipe to write to
 * @buf: Buffer to write from
 * @count: Number of bytes to write
 *
 * Returns: Number of bytes written, -1 if full or error
 */
int pipe_write(pipe_t *pipe, const void *buf, size_t count);

/**
 * pipe_close - Close one end of pipe
 * @pipe: Pipe to close
 *
 * Decrements ref_count, frees pipe if both ends closed
 */
void pipe_close(pipe_t *pipe);

#endif /* _IR0_PIPE_H */
