/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: minix_fs_internal.h
 * Description: Private state and helpers for the split MINIX backend.
 *              Not a public ABI; only fs/minix_*.c include this header.
 */

#pragma once

#include "minix_fs.h"
#include "vfs.h"
#include <ir0/logging.h>
#include <ir0/blockdev.h>
#include <ir0/klog.h>
#include <ir0/clock.h>
#include <ir0/stat.h>
#include <ir0/errno.h>
#include <ir0/console_backend.h>
#include <config.h>
#include <ir0/kmem.h>
#include <ir0/credentials.h>
#include <ir0/permissions.h>
#include <ir0/exec_read_trace.h>
#include <string.h>

#define typewriter_vga_print(msg, color)                                         \
    do {                                                                         \
        const char *__msg = (msg);                                               \
        if (__msg)                                                               \
            console_backend_write(__msg, strlen(__msg), (uint8_t)(color));       \
    } while (0)

#define MINIX_SUPER_MAGIC 0x137F
#define MINIX_MAGIC 0x137F

#define MINIX_ROOT_INODE 1
#define MINIX_MAX_INODES 1024
#define MINIX_MAX_ZONES 65535
#define MINIX_ZONE_SIZE 1024

/* Number of direct zone pointers in a MINIX inode (i_zone[0]..[6]) */
#define MINIX_DIRECT_ZONES 7

#define MINIX_INDIRECT_ZONES (MINIX_BLOCK_SIZE / (int)sizeof(uint16_t))
#define MINIX_DINDIRECT_ZONES \
	((size_t)MINIX_INDIRECT_ZONES * (size_t)MINIX_INDIRECT_ZONES)

typedef struct minix_fs_info
{
  minix_superblock_t superblock;
  uint8_t *inode_bitmap;
  uint8_t *zone_bitmap;
  minix_inode_t *inode_table;
  uint8_t *zone_table;
  bool initialized;
} minix_fs_info_t;

extern minix_fs_info_t minix_fs;
extern bool root_inode_cached;

/* Internal helpers shared across the split implementation. */
void minix_dirent_name(const minix_dir_entry_t *e, char out[MINIX_NAME_LEN + 1]);
uint32_t minix_now_seconds(void);
size_t minix_zmap_bytes(void);
int minix_sync_zone_bitmap(void);
bool minix_root_block_present(void);
int minix_sync_inode_bitmap(void);
int minix_read_inode(uint32_t inode_num, minix_inode_t *inode);
int minix_lookup(const char *pathname, minix_inode_t *out, uint16_t *ino_out);
void minix_inode_truncate_zones(minix_inode_t *inode, uint16_t inode_num,
                                size_t old_size, size_t new_size);

/* VFS backend entry points defined outside fs/minix_fs.c. */
int minix_create(const char *path, mode_t mode);
int minix_truncate(const char *path, size_t length);
int minix_fs_readdir(const char *path, struct vfs_dirent *entries, int max_entries);
int minix_fs_read_file_wrapper(const char *path, void *buf, size_t count,
                               size_t *read_count, off_t offset);
int minix_fs_write_file_wrapper(const char *path, const void *buf, size_t count,
                                size_t *written_count, off_t offset);
int minix_fs_chmod(const char *path, mode_t mode);
int minix_fs_utimens(const char *path, const struct timespec times[2]);
