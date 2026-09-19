/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ktm_block_fake.c
 * Description: KTM fake block backend (fault injection via read count).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/ktm/block_fake.h>
#include <string.h>

#define KTM_FAKE_SECTORS 8u
#define KTM_FAKE_SECTOR_SIZE 512u
#define KTM_FAKE_EIO_ON_READ 3u

static uint8_t g_fake_disk[KTM_FAKE_SECTORS * KTM_FAKE_SECTOR_SIZE];
static unsigned g_read_count;
static int g_registered;
static struct ir0_block_device g_dev;

static int ktm_fake_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	size_t off;
	size_t len;

	(void)ctx;
	if (!g_registered || !buf || count == 0)
		return -EINVAL;
	if (lba + (uint64_t)count > KTM_FAKE_SECTORS)
		return -EINVAL;

	g_read_count++;
	if (g_read_count == KTM_FAKE_EIO_ON_READ)
		return -EIO;

	off = (size_t)lba * KTM_FAKE_SECTOR_SIZE;
	len = (size_t)count * KTM_FAKE_SECTOR_SIZE;
	memcpy(buf, g_fake_disk + off, len);
	return 0;
}

static int ktm_fake_write(void *ctx, uint64_t lba, uint32_t count,
			  const void *buf)
{
	size_t off;
	size_t len;

	(void)ctx;
	if (!g_registered || !buf || count == 0)
		return -EINVAL;
	if (lba + (uint64_t)count > KTM_FAKE_SECTORS)
		return -EINVAL;

	off = (size_t)lba * KTM_FAKE_SECTOR_SIZE;
	len = (size_t)count * KTM_FAKE_SECTOR_SIZE;
	memcpy(g_fake_disk + off, buf, len);
	return 0;
}

static int ktm_fake_flush(void *ctx)
{
	(void)ctx;
	return 0;
}

static const struct ir0_block_ops ktm_fake_ops = {
	.read = ktm_fake_read,
	.write = ktm_fake_write,
	.flush = ktm_fake_flush,
};

int ktm_block_fake_setup(void)
{
	int rc;

	if (g_registered)
	{
		ktm_block_fake_reset_reads();
		return 0;
	}

	memset(g_fake_disk, 0, sizeof(g_fake_disk));
	memset(&g_dev, 0, sizeof(g_dev));
	g_dev.ops = &ktm_fake_ops;
	g_dev.info.sector_size = KTM_FAKE_SECTOR_SIZE;
	g_dev.info.max_sectors_per_io = 4;
	g_dev.info.sector_count = KTM_FAKE_SECTORS;
	strncpy(g_dev.info.name, KTM_BLOCK_FAKE_NAME,
		sizeof(g_dev.info.name) - 1);

	rc = ir0_block_register(&g_dev);
	if (rc < 0)
		return rc;

	g_registered = 1;
	g_read_count = 0;
	return 0;
}

void ktm_block_fake_teardown(void)
{
	g_read_count = 0;
}

void ktm_block_fake_reset_reads(void)
{
	g_read_count = 0;
}

unsigned ktm_block_fake_read_count(void)
{
	return g_read_count;
}
