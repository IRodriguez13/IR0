/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ramtab.h
 * Description: In-memory path table for staged names not yet on a VFS mount
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/early_rootfs.h>

#define RAMTAB_MAX 24
#define RAMTAB_NAME 40
#define RAMTAB_FILE_CAP 512

struct ramtab_ent
{
	int used;
	char path[RAMTAB_NAME];
	uint32_t mode;
	uint32_t size;
	const uint8_t *data;
	int fifo_pipe;
	int writable;
	int link_to;
	uint8_t own[RAMTAB_FILE_CAP];
};

void ramtab_reset(void);
void ramtab_set_rehome_observer(void (*fn)(int old_id, int new_id));
int ramtab_eq(const char *left, const char *right);
int ramtab_parent(const char *path, char *parent, unsigned cap);
const char *ramtab_basename(const char *path);
int ramtab_find(const char *path);
int ramtab_body(int idx);
void ramtab_rehome(int fsid);
int ramtab_add(const char *path, uint32_t mode, uint32_t size,
	       const uint8_t *data, int writable);
struct ramtab_ent *ramtab_get(int idx);
struct ramtab_ent *ramtab_table(void);
int ramtab_child_count(const char *dir);
int ramtab_child_at(const char *dir, int which,
		    struct ir0_early_dirent_spec *spec);
enum ir0_early_rootfs_node ramtab_node_kind(uint32_t mode);
