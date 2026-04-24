/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: block_dev.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Block device registry
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Registro de dispositivos de bloque. Los drivers registran sus dispositivos
 * tras la inicialización; los consumidores (MINIX, devfs, etc.) los usan por nombre.
 */

#include "block_dev.h"
#include <string.h>

#define MAX_BLOCK_DEVS 8
#define MAX_NAME_LEN 16

struct block_dev_entry {
	char name[MAX_NAME_LEN];
	const block_dev_ops_t *ops;
	uint8_t dev_id;
	bool used;
};

static struct block_dev_entry s_devs[MAX_BLOCK_DEVS];
static int s_count;
static const char *const s_legacy_names[] = {"hda", "hdb", "hdc", "hdd"};

int block_dev_register(const char *name, const block_dev_ops_t *ops, uint8_t dev_id)
{
	if (!name || !ops || s_count >= MAX_BLOCK_DEVS)
		return -1;

	size_t len = 0;
	while (name[len] && len < MAX_NAME_LEN - 1)
		len++;
	if (len == 0)
		return -1;

	for (int i = 0; i < MAX_BLOCK_DEVS; i++) {
		if (s_devs[i].used && strcmp(s_devs[i].name, name) == 0)
			return -1;
	}

	int idx = 0;
	for (; idx < MAX_BLOCK_DEVS; idx++) {
		if (!s_devs[idx].used)
			break;
	}
	if (idx >= MAX_BLOCK_DEVS)
		return -1;

	for (int i = 0; name[i] && i < MAX_NAME_LEN - 1; i++)
		s_devs[idx].name[i] = name[i];
	s_devs[idx].name[MAX_NAME_LEN - 1] = '\0';
	s_devs[idx].ops = ops;
	s_devs[idx].dev_id = dev_id;
	s_devs[idx].used = true;
	if (idx >= s_count)
		s_count = idx + 1;
	return 0;
}

const block_dev_ops_t *block_dev_get(const char *name, uint8_t *dev_id_out)
{
	if (!name)
		return NULL;
	for (int i = 0; i < s_count; i++) {
		if (s_devs[i].used && strcmp(s_devs[i].name, name) == 0) {
			if (dev_id_out)
				*dev_id_out = s_devs[i].dev_id;
			return s_devs[i].ops;
		}
	}
	return NULL;
}

bool block_dev_read_sectors(const char *name, uint32_t lba, uint8_t n, void *buf)
{
	uint8_t dev_id;
	const block_dev_ops_t *ops = block_dev_get(name, &dev_id);
	if (!ops || !ops->read_sectors || !buf)
		return false;
	return ops->read_sectors(dev_id, lba, n, buf);
}

bool block_dev_write_sectors(const char *name, uint32_t lba, uint8_t n, const void *buf)
{
	uint8_t dev_id;
	const block_dev_ops_t *ops = block_dev_get(name, &dev_id);
	if (!ops || !ops->write_sectors || !buf)
		return false;
	return ops->write_sectors(dev_id, lba, n, buf);
}

uint64_t block_dev_get_sector_count(const char *name)
{
	uint8_t dev_id;
	const block_dev_ops_t *ops = block_dev_get(name, &dev_id);
	if (!ops || !ops->get_sector_count)
		return 0;
	return ops->get_sector_count(dev_id);
}

bool block_dev_is_present(const char *name)
{
	uint8_t dev_id;
	const block_dev_ops_t *ops = block_dev_get(name, &dev_id);
	if (!ops || !ops->is_present)
		return false;
	return ops->is_present(dev_id);
}

int block_dev_count(void)
{
	return s_count;
}

const char *block_dev_name_at(int index)
{
	if (index < 0 || index >= s_count)
		return NULL;
	if (!s_devs[index].used)
		return NULL;
	return s_devs[index].name;
}

const char *block_dev_legacy_name(uint8_t disk_id)
{
	if (disk_id >= (sizeof(s_legacy_names) / sizeof(s_legacy_names[0])))
		return NULL;
	return s_legacy_names[disk_id];
}
