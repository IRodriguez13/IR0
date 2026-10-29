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

	{
		uint8_t big[3 * MOCK_SEC_SIZE];

		g_mock_reads = 0;
		rc = ir0_block_read(id, 0, 3, big);
		ASSERT_EQ(rc, 0);
		ASSERT_EQ(g_mock_reads, 2);
		ASSERT_EQ(big[0], 0x55);
	}

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

void test_blockdev_duplicate_check_scans_gaps(void)
{
	struct ir0_block_device first;
	struct ir0_block_device second;
	struct ir0_block_device probe;
	dev_t first_id;

	TEST_BEGIN("blockdev_duplicate_check_scans_gaps");

	ir0_block_reset_for_test();

	memset(&first, 0, sizeof(first));
	first.ops = &mock_ops;
	first.info.sector_size = MOCK_SEC_SIZE;
	first.info.max_sectors_per_io = 1;
	first.info.sector_count = MOCK_SECTORS;
	strncpy(first.info.name, "gap0", sizeof(first.info.name) - 1);
	ASSERT_EQ(ir0_block_register(&first), 0);

	memset(&second, 0, sizeof(second));
	second.ops = &mock_ops;
	second.info.sector_size = MOCK_SEC_SIZE;
	second.info.max_sectors_per_io = 1;
	second.info.sector_count = MOCK_SECTORS;
	strncpy(second.info.name, "gap1", sizeof(second.info.name) - 1);
	ASSERT_EQ(ir0_block_register(&second), 0);
	ASSERT_EQ(ir0_block_count(), 2);

	first_id = ir0_block_lookup_by_name("gap0");
	ASSERT(first_id != 0);
	ir0_block_unregister_for_test(first_id);
	ASSERT(!ir0_block_is_present(first_id));
	ASSERT(ir0_block_lookup_by_name("gap0") == 0);

	/*
	 * Slot 0 is free and "gap1" lives in slot 1.  Registration used to
	 * stop scanning at the hole and accept the duplicate.
	 */
	probe = second;
	ASSERT_EQ(ir0_block_register(&probe), -EEXIST);

	/* A unique device still reuses the hole without growing the table. */
	strncpy(probe.info.name, "gap2", sizeof(probe.info.name) - 1);
	ASSERT_EQ(ir0_block_register(&probe), 0);
	ASSERT_EQ(ir0_block_count(), 2);
	ASSERT(ir0_block_lookup_by_name("gap2") != 0);
	ASSERT(ir0_block_name_at(0) != NULL);
	ASSERT(strcmp(ir0_block_name_at(0), "gap2") == 0);
	ASSERT(ir0_block_name_at(1) != NULL);
	ASSERT(strcmp(ir0_block_name_at(1), "gap1") == 0);

	ir0_block_reset_for_test();
	TEST_END();
}
