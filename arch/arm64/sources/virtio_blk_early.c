/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_blk_early.c
 * Description: Virtio-blk modern MMIO backend for portable ir0_block facade.
 */

#include "virtio_blk_early.h"
#include "pl011.h"

#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/virtio_mmio.h>

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <ir0/boot_log.h>

#define VIRTQ_DESC_F_NEXT  1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN  0U
#define VIRTIO_BLK_T_OUT 1U

/* Bit 32 — mandatory for modern (virtio-mmio version 2). */
#define VIRTIO_F_VERSION_1 32U

#define BLK_QUEUE_SIZE 16U
#define BLK_SECTOR     512U

struct virtq_desc
{
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
} __attribute__((packed));

struct virtq_avail
{
	uint16_t flags;
	uint16_t idx;
	uint16_t ring[BLK_QUEUE_SIZE];
	uint16_t used_event;
} __attribute__((packed));

struct virtq_used_elem
{
	uint32_t id;
	uint32_t len;
} __attribute__((packed));

struct virtq_used
{
	uint16_t flags;
	uint16_t idx;
	struct virtq_used_elem ring[BLK_QUEUE_SIZE];
	uint16_t avail_event;
} __attribute__((packed));

struct virtio_blk_req
{
	uint32_t type;
	uint32_t reserved;
	uint64_t sector;
} __attribute__((packed));

static struct virtq_desc g_desc[BLK_QUEUE_SIZE] __attribute__((aligned(16)));
static struct virtq_avail g_avail __attribute__((aligned(2)));
static struct virtq_used g_used __attribute__((aligned(4)));
static struct virtio_blk_req g_req;
static uint8_t g_status;
static struct virtio_mmio_dev *g_blk_dev;
static uint64_t g_capacity;
static int g_ready;

static void zero_bytes(void *p, size_t n)
{
	uint8_t *b = p;

	while (n--)
		*b++ = 0;
}

static int blk_setup_queue(struct virtio_mmio_dev *d)
{
	uint32_t qmax;
	uint32_t qnum;
	uint32_t ready;

	if (d->version < 2U)
	{
		/* Prefer modern: smoke uses -global virtio-mmio.force-legacy=false. */
		return -1;
	}

	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_SEL, 0);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_READY, 0);
	qmax = virtio_mmio_read(d, VIRTIO_MMIO_REG_QUEUE_NUM_MAX);
	if (qmax == 0U)
		return -1;

	qnum = BLK_QUEUE_SIZE;
	if (qnum > qmax)
		qnum = qmax;

	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_NUM, qnum);

	zero_bytes(g_desc, sizeof(g_desc));
	zero_bytes(&g_avail, sizeof(g_avail));
	zero_bytes(&g_used, sizeof(g_used));

	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DESC_LOW,
			  (uint32_t)(uintptr_t)g_desc);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DESC_HIGH,
			  (uint32_t)((uint64_t)(uintptr_t)g_desc >> 32));
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DRIVER_LOW,
			  (uint32_t)(uintptr_t)&g_avail);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DRIVER_HIGH,
			  (uint32_t)((uint64_t)(uintptr_t)&g_avail >> 32));
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DEVICE_LOW,
			  (uint32_t)(uintptr_t)&g_used);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_DEVICE_HIGH,
			  (uint32_t)((uint64_t)(uintptr_t)&g_used >> 32));

	__asm__ volatile("dmb sy" ::: "memory");
	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_READY, 1);
	ready = virtio_mmio_read(d, VIRTIO_MMIO_REG_QUEUE_READY);
	if ((ready & 1U) == 0U)
		return -1;
	return 0;
}

static int blk_xfer_buf(struct virtio_mmio_dev *d, uint32_t type, uint64_t sector,
			void *data)
{
	uint16_t last_used;
	unsigned spins;
	uint16_t aidx;

	if (!d || !data)
		return -EINVAL;

	g_req.type = type;
	g_req.reserved = 0;
	g_req.sector = sector;
	g_status = 0xff;

	g_desc[0].addr = (uint64_t)(uintptr_t)&g_req;
	g_desc[0].len = sizeof(g_req);
	g_desc[0].flags = VIRTQ_DESC_F_NEXT;
	g_desc[0].next = 1;

	g_desc[1].addr = (uint64_t)(uintptr_t)data;
	g_desc[1].len = BLK_SECTOR;
	g_desc[1].flags = VIRTQ_DESC_F_NEXT |
			   (type == VIRTIO_BLK_T_IN ? VIRTQ_DESC_F_WRITE : 0U);
	g_desc[1].next = 2;

	g_desc[2].addr = (uint64_t)(uintptr_t)&g_status;
	g_desc[2].len = 1;
	g_desc[2].flags = VIRTQ_DESC_F_WRITE;
	g_desc[2].next = 0;

	last_used = g_used.idx;
	aidx = g_avail.idx;
	g_avail.ring[aidx % BLK_QUEUE_SIZE] = 0;
	__asm__ volatile("dmb sy" ::: "memory");
	g_avail.idx = (uint16_t)(aidx + 1U);
	__asm__ volatile("dmb sy" ::: "memory");

	virtio_mmio_write(d, VIRTIO_MMIO_REG_QUEUE_NOTIFY, 0);

	for (spins = 0; spins < 2000000U; spins++)
	{
		if (g_used.idx != last_used)
			break;
		__asm__ volatile("yield" ::: "memory");
	}
	if (g_used.idx == last_used)
		return -EIO;
	if (g_status != 0)
		return -EIO;
	return 0;
}

static int virtio_blk_ops_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	struct virtio_mmio_dev *d = ctx;
	uint32_t i;
	uint8_t *p = buf;

	if (!g_ready || !d || !buf || count == 0)
		return -EINVAL;
	if (lba + count > g_capacity)
		return -EINVAL;

	for (i = 0; i < count; i++)
	{
		int ret = blk_xfer_buf(d, VIRTIO_BLK_T_IN, lba + i,
				       p + (size_t)i * BLK_SECTOR);

		if (ret != 0)
			return ret;
	}
	return 0;
}

static int virtio_blk_ops_write(void *ctx, uint64_t lba, uint32_t count,
				const void *buf)
{
	struct virtio_mmio_dev *d = ctx;
	uint32_t i;
	const uint8_t *p = buf;

	if (!g_ready || !d || !buf || count == 0)
		return -EINVAL;
	if (lba + count > g_capacity)
		return -EINVAL;

	for (i = 0; i < count; i++)
	{
		int ret = blk_xfer_buf(d, VIRTIO_BLK_T_OUT, lba + i,
				       (void *)(p + (size_t)i * BLK_SECTOR));

		if (ret != 0)
			return ret;
	}
	return 0;
}

static int virtio_blk_ops_flush(void *ctx)
{
	(void)ctx;
	return 0;
}

static const struct ir0_block_ops g_virtio_blk_ops = {
	.read = virtio_blk_ops_read,
	.write = virtio_blk_ops_write,
	.flush = virtio_blk_ops_flush,
};

static int virtio_blk_bringup(struct virtio_mmio_dev *d)
{
	uint32_t status;
	uint64_t capacity;

	virtio_mmio_set_status(d, 0);
	virtio_mmio_set_status(d, VIRTIO_STATUS_ACKNOWLEDGE);
	virtio_mmio_set_status(d, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 1);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES,
			  (VIRTIO_F_VERSION_1 >= 32U)
				  ? (1U << (VIRTIO_F_VERSION_1 - 32U))
				  : 0U);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 0);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES, 0);
	if (d->version >= 2U)
	{
		status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
			 VIRTIO_STATUS_FEATURES_OK;
		virtio_mmio_set_status(d, status);
		if ((virtio_mmio_get_status(d) & VIRTIO_STATUS_FEATURES_OK) == 0)
			return -1;
	}

	{
		volatile uint32_t *cfg = &d->base[VIRTIO_MMIO_REG_CONFIG / 4U];
		uint32_t lo = cfg[0];
		uint32_t hi = cfg[1];

		capacity = ((uint64_t)hi << 32) | lo;
	}
	if (capacity == 0)
		return -1;

	if (blk_setup_queue(d) != 0)
	{
		ir0_boot_smoke("ARM64_VIRTIO_BLK_CAP_OK");
		return -1;
	}

	virtio_mmio_set_status(d,
			       VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
				       VIRTIO_STATUS_FEATURES_OK |
				       VIRTIO_STATUS_DRIVER_OK);

	g_blk_dev = d;
	g_capacity = capacity;
	g_ready = 1;
	return 0;
}

int arm64_virtio_blk_smoke(void)
{
	struct virtio_mmio_dev *d;
	struct ir0_block_device bdev;
	dev_t id;
	uint8_t wbuf[BLK_SECTOR] __attribute__((aligned(16)));
	uint8_t rbuf[BLK_SECTOR] __attribute__((aligned(16)));
	unsigned i;
	int rc;

	d = arm64_virtio_mmio_find(VIRTIO_ID_BLOCK);
	if (!d)
	{
		ir0_boot_smoke("ARM64_VIRTIO_BLK_FAIL");
		return -1;
	}

	if (virtio_blk_bringup(d) != 0)
	{
		ir0_boot_smoke("ARM64_VIRTIO_BLK_FAIL");
		return -1;
	}

	zero_bytes(&bdev, sizeof(bdev));
	bdev.ctx = d;
	bdev.ops = &g_virtio_blk_ops;
	bdev.info.sector_size = BLK_SECTOR;
	bdev.info.max_sectors_per_io = 1;
	bdev.info.sector_count = g_capacity;
	bdev.info.flags = IR0_BLOCK_FLAG_DMA_CAPABLE;
	/* name "vda" — same portable registry as ATA/AHCI on x64. */
	bdev.info.name[0] = 'v';
	bdev.info.name[1] = 'd';
	bdev.info.name[2] = 'a';
	bdev.info.name[3] = '\0';

	rc = ir0_block_register(&bdev);
	if (rc != 0)
	{
		ir0_boot_smoke("ARM64_VIRTIO_BLK_FAIL");
		return -1;
	}

	ir0_boot_smoke("ARM64_VIRTIO_BLK_OK");

	id = ir0_block_lookup_by_name("vda");
	if (id == 0 || !ir0_block_is_present(id))
	{
		ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_FAIL");
		return -1;
	}

	for (i = 0; i < BLK_SECTOR; i++)
		wbuf[i] = (uint8_t)(0xA5 ^ (uint8_t)i);
	zero_bytes(rbuf, BLK_SECTOR);

	/* Architecture proof: I/O only through portable ir0_block_*. */
	if (ir0_block_write(id, 0, 1, wbuf) != 0)
	{
		ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_FAIL");
		return -1;
	}
	if (ir0_block_read(id, 0, 1, rbuf) != 0)
	{
		ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_FAIL");
		return -1;
	}

	for (i = 0; i < BLK_SECTOR; i++)
	{
		if (rbuf[i] != wbuf[i])
		{
			ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_FAIL");
			return -1;
		}
	}

	ir0_boot_smoke("ARM64_BLOCKDEV_FACADE_OK");
	return 0;
}
