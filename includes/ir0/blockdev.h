/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Block device facade (dev_t + backend ops).
 *
 * Filesystems and VFS use this layer only; ATA/SATA/NVMe register backends.
 */

#ifndef _IR0_BLOCKDEV_H
#define _IR0_BLOCKDEV_H

#include <ir0/types.h>
#include <stdbool.h>
#include <stdint.h>

#define IR0_BLOCK_FLAG_PIO_ONLY     (1u << 0)
#define IR0_BLOCK_FLAG_DMA_CAPABLE  (1u << 1)
#define IR0_BLOCK_FLAG_REMOVABLE    (1u << 2)
#define IR0_BLOCK_FLAG_READONLY     (1u << 3)

#define IR0_BLOCKDEV_NAME_MAX 16

struct ir0_block_ops
{
	int (*read)(void *ctx, uint64_t lba, uint32_t count, void *buf);
	int (*write)(void *ctx, uint64_t lba, uint32_t count, const void *buf);
	int (*flush)(void *ctx);
};

struct ir0_block_info
{
	uint32_t sector_size;
	uint32_t max_sectors_per_io;
	uint64_t sector_count;
	uint32_t flags;
	char name[IR0_BLOCKDEV_NAME_MAX];
};

struct ir0_block_device
{
	dev_t id;
	void *ctx;
	const struct ir0_block_ops *ops;
	struct ir0_block_info info;
};

int ir0_block_register(struct ir0_block_device *dev);
dev_t ir0_block_lookup_by_name(const char *name);
bool ir0_block_is_present(dev_t dev);
int ir0_block_read(dev_t dev, uint64_t lba, uint32_t count, void *buf);
int ir0_block_write(dev_t dev, uint64_t lba, uint32_t count, const void *buf);
int ir0_block_flush(dev_t dev);
int ir0_block_get_info(dev_t dev, struct ir0_block_info *out);
int ir0_block_count(void);
const char *ir0_block_name_at(int index);
dev_t ir0_block_dev_at(int index);

#endif /* _IR0_BLOCKDEV_H */
