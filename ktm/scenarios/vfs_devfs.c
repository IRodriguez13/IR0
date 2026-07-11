/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_devfs.c
 * Description: KTM scenario — devfs null node open/close lifecycle.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/devfs.h>

extern void ensure_devfs_init(void);

static int scenario_vfs_devfs_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	int64_t ret;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	ensure_devfs_init();
	KTM_CHECKPOINT(KTM_CP_VFS_MOUNT);
	ret = devfs_open_node(&dev_null, 0);
	KTM_V1_ASSERT_TRUE(ret == 0);
	ret = devfs_close_node(&dev_null);
	KTM_V1_ASSERT_TRUE(ret == 0);
	KTM_CHECKPOINT(KTM_CP_VFS_UMOUNT);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_vfs_devfs = {
	.name = "vfs.devfs",
	.flags = 0,
	.setup = NULL,
	.run = scenario_vfs_devfs_run,
	.teardown = NULL,
};

void ktm_scenario_register_vfs_devfs(void)
{
	(void)ktm_scenario_register(&scenario_vfs_devfs);
}
