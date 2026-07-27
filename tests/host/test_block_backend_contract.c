/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_block_backend_contract.c
 * Description: block_backend_contract_suite — ir0_block_ops mandatory hooks (§7).
 */

#include "test_harness_ir0.h"
#include <ir0/blockdev.h>
#include <string.h>

static int mock_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	(void)ctx;
	(void)lba;
	(void)count;
	(void)buf;
	return 0;
}

static int mock_write(void *ctx, uint64_t lba, uint32_t count, const void *buf)
{
	(void)ctx;
	(void)lba;
	(void)count;
	(void)buf;
	return 0;
}

static int block_ops_complete(const struct ir0_block_ops *ops)
{
	return ops && ops->read && ops->write;
}

void test_block_backend_contract(void)
{
	struct ir0_block_ops ops;

	TEST_BEGIN("block_backend_contract_mandatory_ops");
	memset(&ops, 0, sizeof(ops));
	ASSERT(!block_ops_complete(&ops));
	ops.read = mock_read;
	ops.write = mock_write;
	ASSERT(block_ops_complete(&ops));
	ASSERT(ops.flush == NULL); /* optional */
	ASSERT(ops.read(NULL, 0, 1, NULL) == 0);
	ASSERT(ops.write(NULL, 0, 1, NULL) == 0);
	TEST_END();
}
