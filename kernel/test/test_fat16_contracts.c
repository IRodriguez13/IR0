/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_fat16_contracts.c
 * Description: ktest cases for the FAT16 filesystem on a block device. The
 *              volume is a small FAT16 image in a RAM block device registered
 *              under a name that is not a legacy ATA slot.
 */

#include "test/ktest_harness.h"
#include "ktest_decl.h"
#include "syscalls.h"
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>
#include <string.h>

#define KTFAT_NAME        "ktfat"
#define KTFAT_SECTOR      512u
#define KTFAT_SECTORS     256u
#define KTFAT_HELLO       "FAT16-KTEST-OK"

/*
 * Layout: boot sector, two one-sector FATs, a one-sector root directory
 * (16 entries) and 252 one-sector clusters. HELLO.TXT holds cluster 2.
 */
#define KTFAT_FAT_LBA     1u
#define KTFAT_ROOT_LBA    3u
#define KTFAT_DATA_LBA    4u

static uint8_t ktfat_data[KTFAT_SECTORS * KTFAT_SECTOR] __attribute__((aligned(8)));
static struct ir0_block_device ktfat_dev;
static int ktfat_registered;

static int ktfat_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	(void)ctx;
	if (!buf || count == 0 || lba + count > KTFAT_SECTORS)
		return -EINVAL;
	memcpy(buf, ktfat_data + lba * KTFAT_SECTOR, (size_t)count * KTFAT_SECTOR);
	return 0;
}

static int ktfat_write(void *ctx, uint64_t lba, uint32_t count, const void *buf)
{
	(void)ctx;
	if (!buf || count == 0 || lba + count > KTFAT_SECTORS)
		return -EINVAL;
	memcpy(ktfat_data + lba * KTFAT_SECTOR, buf, (size_t)count * KTFAT_SECTOR);
	return 0;
}

static const struct ir0_block_ops ktfat_ops = {
	.read = ktfat_read,
	.write = ktfat_write,
	.flush = NULL,
};

static void put16(uint8_t *p, uint16_t v)
{
	p[0] = (uint8_t)v;
	p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v)
{
	put16(p, (uint16_t)v);
	put16(p + 2, (uint16_t)(v >> 16));
}

/* mkfs.fat -F 16 on the RAM disk, plus HELLO.TXT in the root directory. */
static void ktfat_format(void)
{
	uint8_t *boot = ktfat_data;
	uint8_t *root = ktfat_data + KTFAT_ROOT_LBA * KTFAT_SECTOR;
	size_t hello_len = sizeof(KTFAT_HELLO) - 1;

	memset(ktfat_data, 0, sizeof(ktfat_data));

	boot[0] = 0xEB;
	boot[1] = 0x3C;
	boot[2] = 0x90;
	memcpy(boot + 3, "IR0KTEST", 8);
	put16(boot + 0x0B, KTFAT_SECTOR);	/* bytes per sector */
	boot[0x0D] = 1;				/* sectors per cluster */
	put16(boot + 0x0E, 1);			/* reserved sectors */
	boot[0x10] = 2;				/* FATs */
	put16(boot + 0x11, 16);			/* root entries */
	put16(boot + 0x13, KTFAT_SECTORS);	/* total sectors */
	boot[0x15] = 0xF8;			/* media */
	put16(boot + 0x16, 1);			/* sectors per FAT */
	boot[0x24] = 0x80;
	boot[0x26] = 0x29;
	put32(boot + 0x27, 0x1D0F0016u);
	memcpy(boot + 0x2B, "KTFAT      ", 11);
	memcpy(boot + 0x36, "FAT16   ", 8);
	boot[510] = 0x55;
	boot[511] = 0xAA;

	for (uint32_t f = 0; f < 2; f++)
	{
		uint8_t *fat = ktfat_data + (KTFAT_FAT_LBA + f) * KTFAT_SECTOR;

		put16(fat + 0, 0xFFF8);
		put16(fat + 2, 0xFFFF);
		put16(fat + 4, 0xFFFF);	/* cluster 2: HELLO.TXT, one cluster */
	}

	memcpy(root, "HELLO   TXT", 11);
	root[11] = 0x20;
	put16(root + 26, 2);
	put32(root + 28, (uint32_t)hello_len);
	memcpy(ktfat_data + KTFAT_DATA_LBA * KTFAT_SECTOR, KTFAT_HELLO, hello_len);
}

const char *ktest_fat16_source(void)
{
	if (!ktfat_registered)
	{
		ktfat_format();
		memset(&ktfat_dev, 0, sizeof(ktfat_dev));
		ktfat_dev.ops = &ktfat_ops;
		ktfat_dev.info.sector_size = KTFAT_SECTOR;
		ktfat_dev.info.max_sectors_per_io = 8;
		ktfat_dev.info.sector_count = KTFAT_SECTORS;
		memcpy(ktfat_dev.info.name, KTFAT_NAME, sizeof(KTFAT_NAME));
		if (ir0_block_register(&ktfat_dev) != 0)
			return NULL;
		ktfat_registered = 1;
	}
	return "/dev/" KTFAT_NAME;
}

static int64_t ktfat_read_file(const char *path, char *buf, size_t size)
{
	int64_t fd = sys_open(path, KTEST_O_RDONLY, 0);
	int64_t n;

	if (fd < 0)
		return fd;
	memset(buf, 0, size);
	n = sys_read((int)fd, buf, size - 1);
	sys_close((int)fd);
	return n;
}

/*
 * A FAT16 volume on a registered block device mounts whatever its name:
 * the mount reads the image on the device, and what is written there is
 * still on it after the volume is unmounted and mounted again.
 */
void ktest_fat16_mount_any_block_device(void)
{
	const char *src;
	const char *msg = "persist";
	char buf[64];
	stat_t st;
	int64_t fd;
	int mk;

	KTEST_BEGIN("fat16_mount_any_block_device");

	src = ktest_fat16_source();
	KASSERT(src != NULL);
	mk = vfs_mkdir("/mnt", 0755);
	KASSERT(mk == 0 || mk == -EEXIST);
	mk = vfs_mkdir("/mnt/ktfat", 0755);
	KASSERT(mk == 0 || mk == -EEXIST);
	KASSERT_EQ(vfs_stat("/mnt/ktfat", &st), 0);

	KASSERT_EQ(sys_mount(src, "/mnt/ktfat", "fat16", 0, NULL), 0);
	KASSERT_EQ(ktfat_read_file("/mnt/ktfat/HELLO.TXT", buf, sizeof(buf)),
		   (int64_t)strlen(KTFAT_HELLO));
	KASSERT(strcmp(buf, KTFAT_HELLO) == 0);

	fd = sys_open("/mnt/ktfat/NEW.TXT",
		      KTEST_O_CREAT | KTEST_O_RDWR | KTEST_O_TRUNC, 0644);
	KASSERT_GE(fd, 0);
	KASSERT_EQ(sys_write((int)fd, msg, strlen(msg)), (int64_t)strlen(msg));
	sys_close((int)fd);

	KASSERT_EQ(sys_umount("/mnt/ktfat", 0), 0);
	KASSERT_EQ(sys_mount(src, "/mnt/ktfat", "fat16", 0, NULL), 0);
	KASSERT_EQ(ktfat_read_file("/mnt/ktfat/NEW.TXT", buf, sizeof(buf)),
		   (int64_t)strlen(msg));
	KASSERT(strcmp(buf, msg) == 0);
	KASSERT_EQ(sys_umount("/mnt/ktfat", 0), 0);

	KTEST_END();
}
