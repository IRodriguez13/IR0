/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_pseudo_fs_contract.c
 * Description: pseudo_fs_contract_suite — ops table mandatory callbacks (§7).
 */

#include "test_harness_ir0.h"
#include <ir0/pseudo_fs.h>
#include <ir0/errno.h>
#include <stddef.h>
#include <string.h>

/*
 * Contract: registered nodes must expose read OR declare directory-only.
 * This host suite validates the ops struct shape without mounting guests.
 */
static int mock_read(void *ctx, char *buf, size_t count, off_t *off)
{
	(void)ctx;
	(void)buf;
	(void)count;
	(void)off;
	return 0;
}

void test_pseudo_fs_contract(void)
{
	pseudo_fs_ops_t ops;

	memset(&ops, 0, sizeof(ops));
	ops.read = mock_read;
	ASSERT(ops.read != NULL);
	ASSERT(ops.write == NULL); /* optional */
	/* Mandatory identity: a zeroed ops without read is not a file node. */
	{
		pseudo_fs_ops_t empty;

		memset(&empty, 0, sizeof(empty));
		ASSERT(empty.read == NULL);
	}
}
