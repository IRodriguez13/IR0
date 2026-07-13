/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: input_events0.c
 * Description: KTM scenario — /dev/events0 open/close (evdev MVP).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/devfs.h>

extern void ensure_devfs_init(void);

static int scenario_input_events0_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	devfs_node_t *node;
	int64_t ret;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	ensure_devfs_init();
	node = devfs_find_node("/dev/events0");
	KTM_REQUIRE(node != NULL);

	KTM_CHECKPOINT(KTM_CP_VFS_MOUNT);
	ret = devfs_open_node(node, 0);
	KTM_V1_ASSERT_TRUE(ret == 0);
	ret = devfs_close_node(node);
	KTM_V1_ASSERT_TRUE(ret == 0);
	KTM_CHECKPOINT(KTM_CP_VFS_UMOUNT);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_input_events0 = {
	.name = "input.events0",
	.flags = 0,
	.setup = NULL,
	.run = scenario_input_events0_run,
	.teardown = NULL,
};

void ktm_scenario_register_input_events0(void)
{
	(void)ktm_scenario_register(&scenario_input_events0);
}
