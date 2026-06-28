/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: fat16_disk.h
 * Description: IR0 — read-only FAT16 on block devices (BPB + cluster I/O)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/stat.h>
#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>

struct vfs_dirent;

int fat16_disk_mount(const char *dev, const char *mount_dir);
int fat16_disk_umount(const char *mount_dir);
int fat16_disk_path_is_mounted(const char *vfs_path);

int fat16_disk_stat(const char *path, stat_t *buf);
int fat16_disk_readdir(const char *path, struct vfs_dirent *entries, int max);
int fat16_disk_read(const char *path, void *buf, size_t count,
		    size_t *bytes_read, off_t offset);
int fat16_disk_mkdir(const char *path, mode_t mode);
int fat16_disk_create(const char *path, mode_t mode);
int fat16_disk_unlink(const char *path);
int fat16_disk_rmdir(const char *path);
int fat16_disk_link(const char *oldpath, const char *newpath);
int fat16_disk_chown(const char *path, uid_t owner, gid_t group);
int fat16_disk_chmod(const char *path, mode_t mode);
int fat16_disk_write(const char *path, const void *buf, size_t count,
		     size_t *bytes_written, off_t offset);
int fat16_disk_truncate(const char *path, size_t length);

/* ktest: validate BPB layout from a 512-byte boot sector image */
int fat16_disk_probe_bpb(const uint8_t sector[512], uint16_t *bytes_per_sector_out);
