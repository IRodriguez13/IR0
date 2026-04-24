/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ata_block.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — ATA block device registration
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Registra los discos ATA en la capa block_dev. Debe llamarse tras ata_init().
 */

#include "block_dev.h"
#include "ata.h"
#include <drivers/disk/partition.h>
#include <string.h>

static const char *ata_names[] = { "hda", "hdb", "hdc", "hdd" };

static bool ata_block_read(uint8_t dev_id, uint32_t lba, uint8_t n, void *buf)
{
	if (dev_id >= 4)
		return false;
	return ata_read_sectors(dev_id, lba, n, buf);
}

static bool ata_block_write(uint8_t dev_id, uint32_t lba, uint8_t n, const void *buf)
{
	if (dev_id >= 4)
		return false;
	return ata_write_sectors(dev_id, lba, n, buf);
}

static uint64_t ata_block_get_sector_count(uint8_t dev_id)
{
	if (dev_id >= 4)
		return 0;
	return ata_get_size(dev_id);
}

static bool ata_block_is_present(uint8_t dev_id)
{
	if (dev_id >= 4)
		return false;
	return ata_drive_present(dev_id);
}

static const block_dev_ops_t ata_block_ops = {
	.read_sectors = ata_block_read,
	.write_sectors = ata_block_write,
	.get_sector_count = ata_block_get_sector_count,
	.is_present = ata_block_is_present,
};

/**
 * ata_block_register - Registra los discos ATA presentes en block_dev
 *
 * Llamar tras ata_init(). Registra hda, hdb, hdc, hdd según corresponda.
 */
void ata_block_register(void)
{
	for (int i = 0; i < 4; i++) {
		if (ata_drive_present((uint8_t)i)) {
			block_dev_register(ata_names[i], &ata_block_ops, (uint8_t)i);
			read_partition_table((uint8_t)i);
		}
	}
}
