/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: swapfs.c
 * Description: SwapFS implementation - Simple swap file system for memory-to-disk paging
 */

#include "swapfs.h"
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <drivers/serial/serial.h>
#include <fs/vfs.h>
#include <string.h>
#include <ir0/devfs.h>

/* Global SwapFS state */
static struct {
    swapfs_file_t *swap_files;      /* List of active swap files */
    swapfs_stats_t stats;           /* Global swap statistics */
    int initialized;                /* 1 if SwapFS is initialized */
    uint32_t next_page_id;          /* Next available page ID */
} swapfs_state = {0};

/* Forward declarations */
static swapfs_file_t *find_swap_file_by_path(const char *path);
static int allocate_swap_page(swapfs_file_t *swap_file);
static void free_swap_page_internal(swapfs_file_t *swap_file, int page_index);
static int write_swap_header(swapfs_file_t *swap_file);
static int read_swap_header(swapfs_file_t *swap_file);

/**
 * swapfs_init - Initialize the SwapFS subsystem
 * 
 * This function initializes the SwapFS subsystem and registers the
 * /dev/swap character device for user-space interaction.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_init(void)
{
    if (swapfs_state.initialized) {
        return 0;  /* Already initialized */
    }
    
    /* Initialize global state */
    memset(&swapfs_state, 0, sizeof(swapfs_state));
    swapfs_state.next_page_id = 1;  /* Start from 1, 0 is invalid */
    
    /* Initialize device interface */
    int ret = swapfs_device_init();
    if (ret < 0) {
        serial_print("[SWAPFS] Failed to initialize device interface\n");
        return ret;
    }
    
    swapfs_state.initialized = 1;
    
    LOG_INFO("SWAPFS", "SwapFS subsystem initialized");
    serial_print("[SWAPFS] SwapFS subsystem initialized\n");
    
    return 0;
}

/**
 * swapfs_create_swap_file - Create a new swap file
 * @path: Path where to create the swap file
 * @size_mb: Size of swap file in megabytes
 * 
 * Creates a new swap file with the specified size and initializes
 * its header and bitmap structures.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_create_swap_file(const char *path, size_t size_mb)
{
    if (!swapfs_state.initialized) {
        return -ENODEV;
    }
    
    if (!path || size_mb == 0) {
        return -EINVAL;
    }
    
    /* Check if swap file already exists */
    if (find_swap_file_by_path(path)) {
        return -EEXIST;
    }
    
    /* Calculate file size and page count */
    size_t file_size = size_mb * 1024 * 1024;
    uint32_t total_pages = (file_size - sizeof(swapfs_header_t)) / SWAPFS_PAGE_SIZE;
    
    if (total_pages == 0) {
        return -EINVAL;
    }
    
    /* Create the swap file */
    int fd = vfs_open(path, O_CREAT | O_RDWR, 0600);
    if (fd < 0) {
        serial_print("[SWAPFS] Failed to create swap file: ");
        serial_print(path);
        serial_print("\n");
        return fd;
    }
    
    /* Initialize swap file header */
    swapfs_header_t header;
    memset(&header, 0, sizeof(header));
    header.magic = SWAPFS_MAGIC;
    header.version = SWAPFS_VERSION;
    header.page_size = SWAPFS_PAGE_SIZE;
    header.total_pages = total_pages;
    header.used_pages = 0;
    header.free_pages = total_pages;
    header.created_time = 0;  /* TODO: Get current time */
    header.last_access = 0;
    
    /* Write header to file */
    ssize_t written = vfs_write(fd, &header, sizeof(header));
    if (written != sizeof(header)) {
        vfs_close(fd);
        vfs_unlink(path);  /* Clean up on failure */
        return -EIO;
    }
    
    /* Initialize file with zeros (optional, but good practice) */
    char zero_page[SWAPFS_PAGE_SIZE];
    memset(zero_page, 0, sizeof(zero_page));
    
    for (uint32_t i = 0; i < total_pages; i++) {
        written = vfs_write(fd, zero_page, sizeof(zero_page));
        if (written != sizeof(zero_page)) {
            vfs_close(fd);
            vfs_unlink(path);
            return -EIO;
        }
    }
    
    vfs_close(fd);
    
    LOG_INFO_FMT("SWAPFS", "Created swap file: %s (%zu MB, %u pages)", 
                 path, size_mb, total_pages);
    
    return 0;
}

/**
 * swapfs_activate_swap_file - Activate a swap file for use
 * @path: Path to the swap file to activate
 * 
 * Activates a swap file, making it available for swap operations.
 * The file is opened, its header is validated, and it's added to
 * the active swap files list.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_activate_swap_file(const char *path)
{
    if (!swapfs_state.initialized) {
        return -ENODEV;
    }
    
    if (!path) {
        return -EINVAL;
    }
    
    /* Check if already active */
    if (find_swap_file_by_path(path)) {
        return -EBUSY;
    }
    
    /* Check if we've reached the maximum number of swap files */
    int active_count = 0;
    swapfs_file_t *current = swapfs_state.swap_files;
    while (current) {
        active_count++;
        current = current->next;
    }
    
    if (active_count >= SWAPFS_MAX_SWAP_FILES) {
        return -ENOMEM;
    }
    
    /* Allocate swap file descriptor */
    swapfs_file_t *swap_file = kmalloc(sizeof(swapfs_file_t));
    if (!swap_file) {
        return -ENOMEM;
    }
    
    memset(swap_file, 0, sizeof(swapfs_file_t));
    strncpy(swap_file->path, path, sizeof(swap_file->path) - 1);
    
    /* Open the swap file */
    swap_file->fd = vfs_open(path, O_RDWR, 0);
    if (swap_file->fd < 0) {
        kfree(swap_file);
        return swap_file->fd;
    }
    
    /* Read and validate header */
    int ret = read_swap_header(swap_file);
    if (ret < 0) {
        vfs_close(swap_file->fd);
        kfree(swap_file);
        return ret;
    }
    
    /* Allocate bitmap for page tracking */
    size_t bitmap_bytes = (swap_file->header.total_pages + 7) / 8;
    swap_file->bitmap = kmalloc(bitmap_bytes);
    if (!swap_file->bitmap) {
        vfs_close(swap_file->fd);
        kfree(swap_file);
        return -ENOMEM;
    }
    
    memset(swap_file->bitmap, 0, bitmap_bytes);
    swap_file->bitmap_size = bitmap_bytes;
    swap_file->active = 1;
    
    /* Add to active swap files list */
    swap_file->next = swapfs_state.swap_files;
    swapfs_state.swap_files = swap_file;
    
    /* Update global statistics */
    swapfs_state.stats.total_swap_files++;
    swapfs_state.stats.total_swap_size += swap_file->header.total_pages * SWAPFS_PAGE_SIZE;
    
    LOG_INFO_FMT("SWAPFS", "Activated swap file: %s (%u pages)", 
                 path, swap_file->header.total_pages);
    
    return 0;
}

/**
 * swapfs_deactivate_swap_file - Deactivate a swap file
 * @path: Path to the swap file to deactivate
 * 
 * Deactivates a swap file, removing it from the active list.
 * Any pages currently swapped to this file should be swapped
 * back to memory before calling this function.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_deactivate_swap_file(const char *path)
{
    if (!swapfs_state.initialized) {
        return -ENODEV;
    }
    
    if (!path) {
        return -EINVAL;
    }
    
    /* Find the swap file in the active list */
    swapfs_file_t *swap_file = NULL;
    swapfs_file_t *prev = NULL;
    swapfs_file_t *current = swapfs_state.swap_files;
    
    while (current) {
        if (strcmp(current->path, path) == 0) {
            swap_file = current;
            break;
        }
        prev = current;
        current = current->next;
    }
    
    if (!swap_file) {
        return -ENOENT;
    }
    
    /* Check if swap file has pages in use */
    if (swap_file->header.used_pages > 0) {
        LOG_WARNING_FMT("SWAPFS", "Deactivating swap file with %u pages in use", 
                        swap_file->header.used_pages);
    }
    
    /* Remove from active list */
    if (prev) {
        prev->next = swap_file->next;
    } else {
        swapfs_state.swap_files = swap_file->next;
    }
    
    /* Update global statistics */
    swapfs_state.stats.total_swap_files--;
    swapfs_state.stats.total_swap_size -= swap_file->header.total_pages * SWAPFS_PAGE_SIZE;
    swapfs_state.stats.used_swap_size -= swap_file->header.used_pages * SWAPFS_PAGE_SIZE;
    
    /* Close file and free resources */
    vfs_close(swap_file->fd);
    kfree(swap_file->bitmap);
    kfree(swap_file);
    
    LOG_INFO_FMT("SWAPFS", "Deactivated swap file: %s", path);
    
    return 0;
}

/**
 * swapfs_swap_out_page - Swap a page from memory to disk
 * @virtual_addr: Virtual address of the page
 * @page_data: Pointer to page data (4KB)
 * @swap_id: Output parameter for swap page ID
 * 
 * Swaps a page from memory to an available swap file.
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_swap_out_page(uint64_t virtual_addr, void *page_data, uint32_t *swap_id)
{
    if (!swapfs_state.initialized || !page_data || !swap_id) {
        return -EINVAL;
    }
    
    /* Find a swap file with available space */
    swapfs_file_t *swap_file = swapfs_state.swap_files;
    while (swap_file) {
        if (swap_file->active && swap_file->header.free_pages > 0) {
            break;
        }
        swap_file = swap_file->next;
    }
    
    if (!swap_file) {
        return -ENOSPC;  /* No swap space available */
    }
    
    /* Allocate a page in the swap file */
    int page_index = allocate_swap_page(swap_file);
    if (page_index < 0) {
        return page_index;
    }
    
    /* Calculate file offset for this page */
    off_t offset = sizeof(swapfs_header_t) + (page_index * SWAPFS_PAGE_SIZE);
    
    /* Seek to the correct position */
    if (vfs_lseek(swap_file->fd, offset, SEEK_SET) != offset) {
        free_swap_page_internal(swap_file, page_index);
        return -EIO;
    }
    
    /* Write page data to swap file */
    ssize_t written = vfs_write(swap_file->fd, page_data, SWAPFS_PAGE_SIZE);
    if (written != SWAPFS_PAGE_SIZE) {
        free_swap_page_internal(swap_file, page_index);
        return -EIO;
    }
    
    /* Generate unique swap ID */
    *swap_id = swapfs_state.next_page_id++;
    
    /* Update statistics */
    swapfs_state.stats.pages_swapped_out++;
    swapfs_state.stats.swap_operations++;
    swapfs_state.stats.used_swap_size += SWAPFS_PAGE_SIZE;
    
    LOG_DEBUG_FMT("SWAPFS", "Swapped out page: vaddr=0x%lx, swap_id=%u", 
                  virtual_addr, *swap_id);
    
    return 0;
}

/**
 * swapfs_get_stats - Get SwapFS statistics
 * @stats: Output buffer for statistics
 * 
 * Returns: 0 on success, negative error code on failure
 */
int swapfs_get_stats(swapfs_stats_t *stats)
{
    if (!swapfs_state.initialized || !stats) {
        return -EINVAL;
    }
    
    memcpy(stats, &swapfs_state.stats, sizeof(swapfs_stats_t));
    return 0;
}

/* Helper functions */

static swapfs_file_t *find_swap_file_by_path(const char *path)
{
    swapfs_file_t *current = swapfs_state.swap_files;
    while (current) {
        if (strcmp(current->path, path) == 0) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

static int allocate_swap_page(swapfs_file_t *swap_file)
{
    if (!swap_file || swap_file->header.free_pages == 0) {
        return -ENOSPC;
    }
    
    /* Find first free page in bitmap */
    for (uint32_t i = 0; i < swap_file->header.total_pages; i++) {
        uint32_t byte_index = i / 8;
        uint32_t bit_index = i % 8;
        
        if (!(swap_file->bitmap[byte_index] & (1 << bit_index))) {
            /* Found free page, mark as used */
            swap_file->bitmap[byte_index] |= (1 << bit_index);
            swap_file->header.used_pages++;
            swap_file->header.free_pages--;
            
            /* Update header on disk */
            write_swap_header(swap_file);
            
            return i;
        }
    }
    
    return -ENOSPC;
}

static void free_swap_page_internal(swapfs_file_t *swap_file, int page_index)
{
    if (!swap_file || page_index < 0 || page_index >= (int)swap_file->header.total_pages) {
        return;
    }
    
    uint32_t byte_index = page_index / 8;
    uint32_t bit_index = page_index % 8;
    
    /* Mark page as free */
    swap_file->bitmap[byte_index] &= ~(1 << bit_index);
    swap_file->header.used_pages--;
    swap_file->header.free_pages++;
    
    /* Update header on disk */
    write_swap_header(swap_file);
}

static int write_swap_header(swapfs_file_t *swap_file)
{
    if (!swap_file) {
        return -EINVAL;
    }
    
    /* Seek to beginning of file */
    if (vfs_lseek(swap_file->fd, 0, SEEK_SET) != 0) {
        return -EIO;
    }
    
    /* Write header */
    ssize_t written = vfs_write(swap_file->fd, &swap_file->header, sizeof(swap_file->header));
    if (written != sizeof(swap_file->header)) {
        return -EIO;
    }
    
    return 0;
}

static int read_swap_header(swapfs_file_t *swap_file)
{
    if (!swap_file) {
        return -EINVAL;
    }
    
    /* Seek to beginning of file */
    if (vfs_lseek(swap_file->fd, 0, SEEK_SET) != 0) {
        return -EIO;
    }
    
    /* Read header */
    ssize_t read_bytes = vfs_read(swap_file->fd, &swap_file->header, sizeof(swap_file->header));
    if (read_bytes != sizeof(swap_file->header)) {
        return -EIO;
    }
    
    /* Validate header */
    if (swap_file->header.magic != SWAPFS_MAGIC) {
        return -EINVAL;
    }
    
    if (swap_file->header.version != SWAPFS_VERSION) {
        return -ENOTSUP;
    }
    
    if (swap_file->header.page_size != SWAPFS_PAGE_SIZE) {
        return -EINVAL;
    }
    
    return 0;
}