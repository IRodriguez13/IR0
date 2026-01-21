/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: swapfs.h
 * Description: SwapFS - Simple swap file system for memory-to-disk paging
 */

#ifndef _SWAPFS_H
#define _SWAPFS_H

#include <stdint.h>
#include <stddef.h>
#include <ir0/errno.h>

/* SwapFS Configuration */
#define SWAPFS_MAX_SWAP_FILES    4
#define SWAPFS_PAGE_SIZE         4096
#define SWAPFS_MAGIC             0x53574150  /* "SWAP" */
#define SWAPFS_VERSION           1

/* Swap file header structure */
typedef struct {
    uint32_t magic;              /* Magic number (SWAPFS_MAGIC) */
    uint32_t version;            /* SwapFS version */
    uint32_t page_size;          /* Page size (should be 4096) */
    uint32_t total_pages;        /* Total pages in swap file */
    uint32_t used_pages;         /* Currently used pages */
    uint32_t free_pages;         /* Free pages available */
    uint64_t created_time;       /* Creation timestamp */
    uint64_t last_access;        /* Last access timestamp */
    uint8_t  reserved[472];      /* Reserved for future use (total 512 bytes) */
} __attribute__((packed)) swapfs_header_t;

/* Swap page entry */
typedef struct {
    uint32_t page_id;            /* Unique page identifier */
    uint32_t flags;              /* Page flags (dirty, accessed, etc.) */
    uint64_t virtual_addr;       /* Original virtual address */
    uint64_t timestamp;          /* When page was swapped out */
} __attribute__((packed)) swapfs_page_entry_t;

/* Swap file descriptor */
typedef struct swapfs_file {
    int fd;                      /* File descriptor for swap file */
    char path[256];              /* Path to swap file */
    swapfs_header_t header;      /* Swap file header */
    uint8_t *bitmap;             /* Page allocation bitmap */
    size_t bitmap_size;          /* Size of bitmap in bytes */
    int active;                  /* 1 if swap file is active */
    struct swapfs_file *next;    /* Next swap file in list */
} swapfs_file_t;

/* SwapFS statistics */
typedef struct {
    uint32_t total_swap_files;   /* Number of active swap files */
    uint64_t total_swap_size;    /* Total swap space in bytes */
    uint64_t used_swap_size;     /* Used swap space in bytes */
    uint64_t pages_swapped_in;   /* Pages swapped from disk to memory */
    uint64_t pages_swapped_out;  /* Pages swapped from memory to disk */
    uint64_t swap_operations;    /* Total swap operations */
} swapfs_stats_t;

/* Page flags */
#define SWAPFS_PAGE_DIRTY        0x01
#define SWAPFS_PAGE_ACCESSED     0x02
#define SWAPFS_PAGE_LOCKED       0x04
#define SWAPFS_PAGE_COMPRESSED   0x08

/* SwapFS operations */
int swapfs_init(void);
int swapfs_create_swap_file(const char *path, size_t size_mb);
int swapfs_activate_swap_file(const char *path);
int swapfs_deactivate_swap_file(const char *path);
int swapfs_swap_out_page(uint64_t virtual_addr, void *page_data, uint32_t *swap_id);
int swapfs_swap_in_page(uint32_t swap_id, void *page_data, uint64_t *virtual_addr);
int swapfs_free_swap_page(uint32_t swap_id);
int swapfs_get_stats(swapfs_stats_t *stats);

/* Device interface functions */
int swapfs_device_init(void);
int swapfs_device_open(const char *path, int flags);
int swapfs_device_close(int fd);
ssize_t swapfs_device_read(int fd, void *buf, size_t count);
ssize_t swapfs_device_write(int fd, const void *buf, size_t count);
int swapfs_device_ioctl(int fd, unsigned int cmd, unsigned long arg);

/* IOCTL commands for /dev/swap */
#define SWAPFS_IOCTL_CREATE      0x5301  /* Create swap file */
#define SWAPFS_IOCTL_ACTIVATE    0x5302  /* Activate swap file */
#define SWAPFS_IOCTL_DEACTIVATE  0x5303  /* Deactivate swap file */
#define SWAPFS_IOCTL_STATS       0x5304  /* Get swap statistics */
#define SWAPFS_IOCTL_LIST        0x5305  /* List active swap files */

/* IOCTL argument structures */
typedef struct {
    char path[256];
    size_t size_mb;
} swapfs_create_args_t;

typedef struct {
    char path[256];
} swapfs_activate_args_t;

#endif /* _SWAPFS_H */