/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: block_read_eio.c
 * Description: KTM scenario — fake blockdev returns -EIO once, then recovers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/ktm/block_fake.h>

static int scenario_block_read_eio_setup(ktm_context_t *ctx)
{
	(void)ctx;
	return ktm_block_fake_setup();
}

static int scenario_block_read_eio_run(ktm_context_t *ctx)
{
	dev_t id;
	uint8_t buf[512];
	int rc;

	(void)ctx;
	id = ir0_block_lookup_by_name(KTM_BLOCK_FAKE_NAME);
	KTM_REQUIRE(id != 0);

	rc = ir0_block_read(id, 0, 1, buf);
	KTM_V1_ASSERT_TRUE(rc == 0);

	rc = ir0_block_read(id, 0, 1, buf);
	KTM_V1_ASSERT_TRUE(rc == 0);

	rc = ir0_block_read(id, 0, 1, buf);
	KTM_V1_ASSERT_TRUE(rc == -EIO);

	rc = ir0_block_read(id, 0, 1, buf);
	KTM_V1_ASSERT_TRUE(rc == 0);
	KTM_V1_ASSERT_TRUE(ktm_block_fake_read_count() == 4u);

	return KTM_OK;
}

static void scenario_block_read_eio_teardown(ktm_context_t *ctx)
{
	(void)ctx;
	ktm_block_fake_teardown();
}

static const ktm_scenario_t scenario_block_read_eio = {
	.name = "block.read_eio_once",
	.flags = 0,
	.setup = scenario_block_read_eio_setup,
	.run = scenario_block_read_eio_run,
	.teardown = scenario_block_read_eio_teardown,
};

void ktm_scenario_register_block_read_eio(void)
{
	(void)ktm_scenario_register(&scenario_block_read_eio);
}
