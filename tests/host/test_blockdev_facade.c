/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * Host contract tests for ir0_block_* facade (P1-storage #11).
 */

#include "test_harness_ir0.h"
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <string.h>

#define MOCK_SECTORS 8
#define MOCK_SEC_SIZE 512

static uint8_t g_mock_disk[MOCK_SECTORS * MOCK_SEC_SIZE];
static int g_mock_reads;
static int g_mock_writes;

static int mock_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	(void)ctx;
	if (lba + count > MOCK_SECTORS)
		return -EINVAL;
	memcpy(buf, g_mock_disk + (size_t)lba * MOCK_SEC_SIZE,
	       (size_t)count * MOCK_SEC_SIZE);
	g_mock_reads++;
	return 0;
}

static int mock_write(void *ctx, uint64_t lba, uint32_t count, const void *buf)
{
	(void)ctx;
	if (lba + count > MOCK_SECTORS)
		return -EINVAL;
	memcpy(g_mock_disk + (size_t)lba * MOCK_SEC_SIZE, buf,
	       (size_t)count * MOCK_SEC_SIZE);
	g_mock_writes++;
	return 0;
}

static int mock_flush(void *ctx)
{
	(void)ctx;
	return 0;
}

static const struct ir0_block_ops mock_ops = {
	.read = mock_read,
	.write = mock_write,
	.flush = mock_flush,
};

static const struct ir0_block_ops mock_ro_ops = {
	.read = mock_read,
	.write = NULL,
	.flush = NULL,
};

void test_blockdev_facade_contract(void)
{
	struct ir0_block_device dev;
	struct ir0_block_info info;
	dev_t id;
	uint8_t buf[MOCK_SEC_SIZE];
	int rc;

	TEST_BEGIN("blockdev_facade_register_io");

	ir0_block_reset_for_test();
	memset(g_mock_disk, 0, sizeof(g_mock_disk));
	g_mock_reads = 0;
	g_mock_writes = 0;
	g_mock_disk[0] = 0x55;
	g_mock_disk[1] = 0xAA;

	memset(&dev, 0, sizeof(dev));
	dev.ops = &mock_ops;
	dev.info.sector_size = MOCK_SEC_SIZE;
	dev.info.max_sectors_per_io = 2;
	dev.info.sector_count = MOCK_SECTORS;
	strncpy(dev.info.name, "mock0", sizeof(dev.info.name) - 1);

	rc = ir0_block_register(&dev);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(ir0_block_count(), 1);

	id = ir0_block_lookup_by_name("mock0");
	ASSERT(id != 0);
	ASSERT(ir0_block_is_present(id));

	rc = ir0_block_get_info(id, &info);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(info.sector_size, MOCK_SEC_SIZE);
	ASSERT_EQ(info.sector_count, (uint64_t)MOCK_SECTORS);

	memset(buf, 0, sizeof(buf));
	rc = ir0_block_read(id, 0, 1, buf);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(buf[0], 0x55);
	ASSERT_EQ(buf[1], 0xAA);
	ASSERT_EQ(g_mock_reads, 1);

	memset(buf, 0xAB, sizeof(buf));
	rc = ir0_block_write(id, 1, 1, buf);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(g_mock_writes, 1);
	ASSERT_EQ(g_mock_disk[MOCK_SEC_SIZE], 0xAB);

	rc = ir0_block_flush(id);
	ASSERT_EQ(rc, 0);

	rc = ir0_block_register(&dev);
	ASSERT_EQ(rc, -EEXIST);

	TEST_END();
}

void test_blockdev_readonly_allows_read(void)
{
	struct ir0_block_device dev;
	dev_t id;
	uint8_t buf[MOCK_SEC_SIZE];
	int rc;

	TEST_BEGIN("blockdev_readonly_flag_read_ok");

	ir0_block_reset_for_test();
	memset(g_mock_disk, 0x11, sizeof(g_mock_disk));
	g_mock_reads = 0;
	g_mock_writes = 0;

	memset(&dev, 0, sizeof(dev));
	dev.ops = &mock_ro_ops;
	dev.info.sector_size = MOCK_SEC_SIZE;
	dev.info.sector_count = MOCK_SECTORS;
	dev.info.flags = IR0_BLOCK_FLAG_READONLY;
	strncpy(dev.info.name, "rom0", sizeof(dev.info.name) - 1);

	rc = ir0_block_register(&dev);
	ASSERT_EQ(rc, 0);
	id = ir0_block_lookup_by_name("rom0");
	ASSERT(id != 0);

	rc = ir0_block_read(id, 0, 1, buf);
	ASSERT_EQ(rc, 0);
	ASSERT_EQ(buf[0], 0x11);
	ASSERT_EQ(g_mock_reads, 1);

	rc = ir0_block_write(id, 0, 1, buf);
	ASSERT_EQ(rc, -EROFS);
	ASSERT_EQ(g_mock_writes, 0);

	TEST_END();
}
