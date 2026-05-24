/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — ATA backend registration for the blockdev facade.
 */

#include "ata.h"
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/serial_io.h>
#include <drivers/disk/partition.h>
#include <string.h>

static const char *ata_names[] = { "hda", "hdb", "hdc", "hdd" };

static int ata_backend_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	uint8_t drive = (uint8_t)(uintptr_t)ctx;

	if (drive >= 4 || !buf || count == 0 || count > 255)
		return -EINVAL;
	if (!ata_read_sectors(drive, (uint32_t)lba, (uint8_t)count, buf))
		return -EIO;
	return 0;
}

static int ata_backend_write(void *ctx, uint64_t lba, uint32_t count,
			     const void *buf)
{
	uint8_t drive = (uint8_t)(uintptr_t)ctx;

	if (drive >= 4 || !buf || count == 0 || count > 255)
		return -EINVAL;
	if (!ata_write_sectors(drive, (uint32_t)lba, (uint8_t)count, buf))
		return -EIO;
	return 0;
}

static const struct ir0_block_ops ata_block_ops = {
	.read = ata_backend_read,
	.write = ata_backend_write,
	.flush = NULL,
};

static int ata_backend_classified;

/**
 * ata_block_register - Register ATA disks with the blockdev facade.
 */
void ata_block_register(void)
{
	int i;

	for (i = 0; i < 4; i++)
	{
		struct ir0_block_device dev;
		int n;

		if (!ata_drive_present((uint8_t)i))
			continue;

		memset(&dev, 0, sizeof(dev));
		dev.ctx = (void *)(uintptr_t)i;
		dev.ops = &ata_block_ops;
		dev.info.sector_size = ATA_SECTOR_SIZE;
		dev.info.max_sectors_per_io = 1;
		dev.info.sector_count = ata_get_size((uint8_t)i);
		dev.info.flags = IR0_BLOCK_FLAG_PIO_ONLY;
		n = 0;
		while (ata_names[i][n] && n < IR0_BLOCKDEV_NAME_MAX - 1)
		{
			dev.info.name[n] = ata_names[i][n];
			n++;
		}
		dev.info.name[n] = '\0';

		if (ir0_block_register(&dev) != 0)
			continue;

		if (!ata_backend_classified)
		{
			ata_backend_classified = 1;
			serial_print("[ATA][CLASSIFY] ATA_BACKEND_ONLY\n");
		}

		read_partition_table((uint8_t)i);
	}
}
