/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: ipc.h
 * Description: IPC channel facade (portable consumers include this header).
 */

#ifndef IR0_IPC_H
#define IR0_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ir0/process.h>

#ifndef _SSIZE_T
#define _SSIZE_T
typedef int64_t ssize_t;
#endif

#define IPC_CHANNEL_BUFFER_SIZE 4096
#define IPC_MAX_CHANNELS 64
#define IPC_INVALID_ID 0xFFFFFFFFu

typedef struct wait_queue_node
{
	process_t *process;
	struct wait_queue_node *next;
} wait_queue_node_t;

typedef struct wait_queue
{
	wait_queue_node_t *head;
	wait_queue_node_t *tail;
} wait_queue_t;

void wait_queue_init(wait_queue_t *wq);
void wait_queue_add(wait_queue_t *wq, process_t *proc);
process_t *wait_queue_wake_one(wait_queue_t *wq);
void wait_queue_wake_all(wait_queue_t *wq);

/* Drop @proc from every IPC channel wait queue (process exit). */
void ipc_purge_waiters_for_process(process_t *proc);

typedef struct semaphore
{
	int count;
	wait_queue_t wait_queue;
} semaphore_t;

void semaphore_init(semaphore_t *sem, int initial_count);
void semaphore_up(semaphore_t *sem);

typedef struct ring_buffer
{
	uint8_t *buffer;
	size_t size;
	size_t read_pos;
	size_t write_pos;
	size_t count;
} ring_buffer_t;

int ring_buffer_init(ring_buffer_t *rb, size_t size);
void ring_buffer_destroy(ring_buffer_t *rb);
size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len);
size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len);
size_t ring_buffer_available_write(ring_buffer_t *rb);
size_t ring_buffer_available_read(ring_buffer_t *rb);
bool ring_buffer_empty(ring_buffer_t *rb);
bool ring_buffer_full(ring_buffer_t *rb);

typedef struct ipc_channel
{
	uint32_t id;
	ring_buffer_t rb;
	int readers;
	int writers;
	semaphore_t sem_read;
	semaphore_t sem_write;
	wait_queue_t read_queue;
	wait_queue_t write_queue;
	volatile uint8_t lock;
	int ref_count;
	struct ipc_channel *next;
} ipc_channel_t;

ipc_channel_t *ipc_channel_create(uint32_t id);
void ipc_channel_destroy(ipc_channel_t *channel);
ipc_channel_t *ipc_channel_find(uint32_t id);
ipc_channel_t *ipc_channel_get_or_create(uint32_t id);
uint32_t ipc_allocate_channel_id(void);

ssize_t ipc_channel_read(ipc_channel_t *channel, void *buf, size_t count);
ssize_t ipc_channel_write(ipc_channel_t *channel, const void *buf, size_t count);

void ipc_channel_ref(ipc_channel_t *channel);
void ipc_channel_unref(ipc_channel_t *channel);

int ipc_init(void);

#endif /* IR0_IPC_H */
