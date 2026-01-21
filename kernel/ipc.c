/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - IPC Implementation
 * Copyright (C) 2025 Iván Rodriguez
 */

#include "ipc.h"
#include "process.h"
#include <ir0/kmem.h>
#include <ir0/oops.h>
#include <ir0/logging.h>
#include <string.h>
#include <kernel/rr_sched.h>

static ipc_channel_t *ipc_channels = NULL;  /* Linked list of all channels */
static uint32_t next_channel_id = 1;



void wait_queue_init(wait_queue_t *wq)
{
    wq->head = NULL;
    wq->tail = NULL;
}

void wait_queue_add(wait_queue_t *wq, process_t *proc)
{
    if (!wq || !proc)
        return;

    wait_queue_node_t *node = kmalloc(sizeof(wait_queue_node_t));
    if (!node)
        return;

    node->process = proc;
    node->next = NULL;

    if (!wq->head) {
        wq->head = node;
        wq->tail = node;
    } else {
        wq->tail->next = node;
        wq->tail = node;
    }

    /* Mark process as blocked */
    proc->state = PROCESS_BLOCKED;
}

process_t *wait_queue_wake_one(wait_queue_t *wq)
{
    if (!wq || !wq->head)
        return NULL;

    wait_queue_node_t *node = wq->head;
    wq->head = node->next;
    
    if (!wq->head)
        wq->tail = NULL;

    process_t *proc = node->process;
    kfree(node);

    /* Mark process as ready to run */
    if (proc)
        proc->state = PROCESS_READY;

    return proc;
}

void wait_queue_wake_all(wait_queue_t *wq)
{
    if (!wq)
        return;

    while (wq->head) {
        wait_queue_node_t *node = wq->head;
        wq->head = node->next;

        if (node->process)
            node->process->state = PROCESS_READY;

        kfree(node);
    }

    wq->tail = NULL;
}

bool wait_queue_empty(wait_queue_t *wq)
{
    return !wq || !wq->head;
}


void semaphore_init(semaphore_t *sem, int initial_count)
{
    if (!sem)
        return;

    sem->count = initial_count;
    wait_queue_init(&sem->wait_queue);
}

void semaphore_down(semaphore_t *sem)
{
    if (!sem)
        return;

    /* Spinlock simulation: wait for lock (simple busy-wait for now) */
    while (sem->count == 0) {
        if (current_process) {
            wait_queue_add(&sem->wait_queue, current_process);
            
            /* Switch to another process */
            rr_schedule_next();
            
            /* When we wake up, sem->count might be > 0 */
            if (sem->count > 0)
                break;
        } else {
            /* Kernel context - just busy wait (shouldn't happen) */
            break;
        }
    }

    if (sem->count > 0)
        sem->count--;
}

void semaphore_up(semaphore_t *sem)
{
    if (!sem)
        return;

    sem->count++;

    /* Wake up one waiting process */
    process_t *woken = wait_queue_wake_one(&sem->wait_queue);
    if (woken) {
        /* Add to scheduler if not already there */
        rr_add_process(woken);
    }
}


int ring_buffer_init(ring_buffer_t *rb, size_t size)
{
    if (!rb || size == 0)
        return -1;

    rb->buffer = kmalloc(size);
    if (!rb->buffer)
        return -1;

    rb->size = size;
    rb->read_pos = 0;
    rb->write_pos = 0;
    rb->count = 0;

    return 0;
}

void ring_buffer_destroy(ring_buffer_t *rb)
{
    if (!rb)
        return;

    if (rb->buffer) {
        kfree(rb->buffer);
        rb->buffer = NULL;
    }
}

size_t ring_buffer_available_write(ring_buffer_t *rb)
{
    if (!rb)
        return 0;
    return rb->size - rb->count;
}

size_t ring_buffer_available_read(ring_buffer_t *rb)
{
    if (!rb)
        return 0;
    return rb->count;
}

bool ring_buffer_empty(ring_buffer_t *rb)
{
    return !rb || rb->count == 0;
}

bool ring_buffer_full(ring_buffer_t *rb)
{
    return !rb || rb->count >= rb->size;
}

size_t ring_buffer_write(ring_buffer_t *rb, const void *data, size_t len)
{
    if (!rb || !data || len == 0)
        return 0;

    size_t available = ring_buffer_available_write(rb);
    size_t to_write = (len < available) ? len : available;

    const uint8_t *src = (const uint8_t *)data;
    for (size_t i = 0; i < to_write; i++) {
        rb->buffer[rb->write_pos] = src[i];
        rb->write_pos = (rb->write_pos + 1) % rb->size;
    }

    rb->count += to_write;
    return to_write;
}

size_t ring_buffer_read(ring_buffer_t *rb, void *data, size_t len)
{
    if (!rb || !data || len == 0)
        return 0;

    size_t available = ring_buffer_available_read(rb);
    size_t to_read = (len < available) ? len : available;

    uint8_t *dest = (uint8_t *)data;
    for (size_t i = 0; i < to_read; i++) {
        dest[i] = rb->buffer[rb->read_pos];
        rb->read_pos = (rb->read_pos + 1) % rb->size;
    }

    rb->count -= to_read;
    return to_read;
}

/* ========================================================================== */
/* IPC CHANNEL IMPLEMENTATION                                                 */
/* ========================================================================== */

ipc_channel_t *ipc_channel_create(uint32_t id)
{
    ipc_channel_t *channel = kmalloc(sizeof(ipc_channel_t));
    if (!channel)
        return NULL;

    /* Initialize ring buffer */
    if (ring_buffer_init(&channel->rb, IPC_CHANNEL_BUFFER_SIZE) != 0) {
        kfree(channel);
        return NULL;
    }

    channel->id = id;
    channel->readers = 0;
    channel->writers = 0;

    /* Initialize semaphores:
     * - sem_read: starts at 0 (no data to read)
     * - sem_write: starts at buffer size (can write that much)
     */
    semaphore_init(&channel->sem_read, 0);
    semaphore_init(&channel->sem_write, IPC_CHANNEL_BUFFER_SIZE);

    wait_queue_init(&channel->read_queue);
    wait_queue_init(&channel->write_queue);

    channel->lock = false;
    channel->ref_count = 0;
    channel->next = NULL;

    LOG_INFO_FMT("IPC", "Created IPC channel %u", id);

    return channel;
}

void ipc_channel_destroy(ipc_channel_t *channel)
{
    if (!channel)
        return;

    LOG_INFO_FMT("IPC", "Destroying IPC channel %u", channel->id);

    /* Wake up all waiting processes */
    wait_queue_wake_all(&channel->read_queue);
    wait_queue_wake_all(&channel->write_queue);

    /* Clean up ring buffer */
    ring_buffer_destroy(&channel->rb);

    kfree(channel);
}

ipc_channel_t *ipc_channel_find(uint32_t id)
{
    ipc_channel_t *channel = ipc_channels;
    while (channel) {
        if (channel->id == id)
            return channel;
        channel = channel->next;
    }
    return NULL;
}

/**
 * Get next available channel ID
 * Scans for gaps or returns next sequential ID
 */
static uint32_t ipc_get_next_available_id(void)
{
    /* Find next available ID by checking if ID exists */
    uint32_t candidate_id = next_channel_id;
    
    /* Try up to 65536 IDs (avoid infinite loop) */
    for (uint32_t attempts = 0; attempts < 65536; attempts++)
    {
        if (ipc_channel_find(candidate_id) == NULL)
        {
            /* Found available ID */
            if (candidate_id >= next_channel_id)
            {
                next_channel_id = candidate_id + 1;
            }
            return candidate_id;
        }
        candidate_id++;
    }
    
    /* Fallback: return current next_channel_id */
    return next_channel_id++;
}

ipc_channel_t *ipc_channel_get_or_create(uint32_t id)
{
    ipc_channel_t *channel = ipc_channel_find(id);
    
    if (!channel) {
        channel = ipc_channel_create(id);
        if (channel) {
            /* Add to global list */
            channel->next = ipc_channels;
            ipc_channels = channel;
            /* Update next_channel_id if this ID is >= current */
            if (id >= next_channel_id)
            {
                next_channel_id = id + 1;
            }
        }
    }

    return channel;
}

uint32_t ipc_allocate_channel_id(void)
{
    return ipc_get_next_available_id();
}

void ipc_channel_ref(ipc_channel_t *channel)
{
    if (!channel)
        return;
    channel->ref_count++;
}

void ipc_channel_unref(ipc_channel_t *channel)
{
    if (!channel)
        return;

    channel->ref_count--;

    /* Auto-destroy when no references left */
    if (channel->ref_count <= 0) {
        /* Remove from global list */
        if (ipc_channels == channel) {
            ipc_channels = channel->next;
        } else {
            ipc_channel_t *prev = ipc_channels;
            while (prev && prev->next != channel)
                prev = prev->next;
            if (prev)
                prev->next = channel->next;
        }

        ipc_channel_destroy(channel);
    }
}

/* Simple spinlock (busy-wait for now) */
static void ipc_lock(ipc_channel_t *channel)
{
    if (!channel)
        return;
    while (channel->lock) {
        /* Busy wait */
    }
    channel->lock = true;
}

static void ipc_unlock(ipc_channel_t *channel)
{
    if (!channel)
        return;
    channel->lock = false;
}

ssize_t ipc_channel_read(ipc_channel_t *channel, void *buf, size_t count)
{
    if (!channel || !buf || count == 0)
        return -1;

    if (!current_process)
        return -1;

    channel->readers++;

    /* Wait until data is available */
    while (ring_buffer_empty(&channel->rb)) {
        wait_queue_add(&channel->read_queue, current_process);
        
        rr_schedule_next();
        
        /* Check if we were woken up with data */
        if (!ring_buffer_empty(&channel->rb))
            break;
    }

    /* Lock and read */
    ipc_lock(channel);
    
    size_t bytes_read = ring_buffer_read(&channel->rb, buf, count);
    
    /* Signal writers that space is available */
    semaphore_up(&channel->sem_write);
    
    ipc_unlock(channel);

    channel->readers--;

    return (ssize_t)bytes_read;
}

ssize_t ipc_channel_write(ipc_channel_t *channel, const void *buf, size_t count)
{
    if (!channel || !buf || count == 0)
        return -1;

    if (!current_process)
        return -1;

    channel->writers++;

    /* Wait until space is available */
    while (ring_buffer_full(&channel->rb)) {
        wait_queue_add(&channel->write_queue, current_process);
        
        rr_schedule_next();
        
        /* Check if we were woken up with space */
        if (!ring_buffer_full(&channel->rb))
            break;
    }

    /* Lock and write */
    ipc_lock(channel);
    
    size_t bytes_written = ring_buffer_write(&channel->rb, buf, count);
    
    /* Signal readers that data is available */
    semaphore_up(&channel->sem_read);
    
    ipc_unlock(channel);

    channel->writers--;

    return (ssize_t)bytes_written;
}

/* ========================================================================== */
/* IPC INITIALIZATION                                                         */
/* ========================================================================== */

int ipc_init(void)
{
    LOG_INFO("IPC", "Initializing IPC subsystem");
    ipc_channels = NULL;
    next_channel_id = 1;
    return 0;
}

