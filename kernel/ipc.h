/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - IPC (Inter-Process Communication)
 * Copyright (C) 2026 Iván Rodriguez
 *
 * IPC Channels: message + sync primitive, kernel-mediated, deterministic, simple Exposed via VFS (/dev/ipc*)
 * 
 */

#ifndef _KERNEL_IPC_H
#define _KERNEL_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "process.h"

/* ssize_t is POSIX signed size type */
#ifndef _SSIZE_T
#define _SSIZE_T
typedef int64_t ssize_t;
#endif



#define IPC_CHANNEL_BUFFER_SIZE 4096  /* 4KB ring buffer per channel */
#define IPC_MAX_CHANNELS 64           /* Maximum number of IPC channels */
#define IPC_INVALID_ID 0xFFFFFFFF     /* Invalid channel ID */



/* Simple wait queue: list of blocked processes waiting for an event */
typedef struct wait_queue_node {
    process_t *process;
    struct wait_queue_node *next;
} wait_queue_node_t;

typedef struct wait_queue {
    wait_queue_node_t *head;
    wait_queue_node_t *tail;
} wait_queue_t;

/* Wait queue operations */
void wait_queue_init(wait_queue_t *wq);
void wait_queue_add(wait_queue_t *wq, process_t *proc);
process_t *wait_queue_wake_one(wait_queue_t *wq);
void wait_queue_wake_all(wait_queue_t *wq);
bool wait_queue_empty(wait_queue_t *wq);



/* Simple semaphore: counter + wait queue */
typedef struct semaphore {
    int count;
    wait_queue_t wait_queue;
} semaphore_t;

/* Semaphore operations */
void semaphore_init(semaphore_t *sem, int initial_count);
void semaphore_down(semaphore_t *sem);  /* Block if count == 0 */
void semaphore_up(semaphore_t *sem);    /* Increment and wake one */



typedef struct ring_buffer {
    uint8_t *buffer;
    size_t size;
    size_t read_pos;
    size_t write_pos;
    size_t count;  /* Number of bytes currently in buffer */
} ring_buffer_t;

/* Ring buffer operations */
int ring_buffer_init(ring_buffer_t *rb, size_t size);
void ring_buffer_destroy(ring_buffer_t *rb);
size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len);
size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len);
size_t ring_buffer_available_write(ring_buffer_t *rb);
size_t ring_buffer_available_read(ring_buffer_t *rb);
bool ring_buffer_empty(ring_buffer_t *rb);
bool ring_buffer_full(ring_buffer_t *rb);



/* IPC Channel: synchronization endpoint + messaging */
typedef struct ipc_channel {
    uint32_t id;                    /* Channel identifier */
    ring_buffer_t rb;               /* Ring buffer for messages */
    
    int readers;                    /* Number of processes reading */
    int writers;                    /* Number of processes writing */
    
    semaphore_t sem_read;           /* Semaphore for readers */
    semaphore_t sem_write;          /* Semaphore for writers */
    
    wait_queue_t read_queue;        /* Processes waiting to read */
    wait_queue_t write_queue;       /* Processes waiting to write */
    
    bool lock;                      /* Simple spinlock (boolean for now) */
    int ref_count;                  /* Reference count (number of open fds) */
    
    struct ipc_channel *next;       /* Linked list */
} ipc_channel_t;


/* Channel management */
ipc_channel_t *ipc_channel_create(uint32_t id);
void ipc_channel_destroy(ipc_channel_t *channel);
ipc_channel_t *ipc_channel_find(uint32_t id);
ipc_channel_t *ipc_channel_get_or_create(uint32_t id);
uint32_t ipc_allocate_channel_id(void);

/* I/O operations (blocking) */
ssize_t ipc_channel_read(ipc_channel_t *channel, void *buf, size_t count);
ssize_t ipc_channel_write(ipc_channel_t *channel, const void *buf, size_t count);

/* Reference counting */
void ipc_channel_ref(ipc_channel_t *channel);
void ipc_channel_unref(ipc_channel_t *channel);



int ipc_init(void);

#endif /* _KERNEL_IPC_H */

