/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: partition.h
 * Description: Disk partition table facade for fs/ — no drivers/ includes.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

typedef struct
{
	uint8_t disk_id;
	uint8_t partition_number;
	uint8_t is_gpt;
	uint8_t system_id;
	uint64_t start_lba;
	uint64_t end_lba;
	uint64_t total_sectors;
	uint8_t type_guid[16];
	uint8_t unique_guid[16];
	uint8_t bootable;
} partition_info_t;

#define MAX_PARTITIONS_PER_DISK 128
#define MAX_DISKS 4
#define MAX_TOTAL_PARTITIONS (MAX_PARTITIONS_PER_DISK * MAX_DISKS)

int read_partition_table(uint8_t disk_id);
int is_gpt_disk(uint8_t disk_id);
const char *get_partition_type(uint8_t system_id);
int get_partition_count(uint8_t disk_id);
int get_partition_info(uint8_t disk_id, uint8_t partition_num, partition_info_t *info);
int partition_nth_on_disk(uint8_t disk_id, unsigned ordinal, partition_info_t *info);
