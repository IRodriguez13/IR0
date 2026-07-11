/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: vfs_open_flags.c
 * Description: KTM scenario — Linux open flags → IR0 classify.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/open_flags.h>

static int scenario_vfs_open_flags_run(ktm_context_t *ctx)
{
	int ir0;

	(void)ctx;

	ir0 = linux_open_flags_to_ir0(IR0_O_RDONLY);
	KTM_V1_ASSERT_TRUE(ir0 == IR0_O_RDONLY);
	KTM_V1_ASSERT_TRUE(ir0_open_flags_ok_for_vfs(ir0));

	ir0 = linux_open_flags_to_ir0(IR0_O_RDWR | (int)LINUX_O_CLOEXEC);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_ACCMODE) == IR0_O_RDWR);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_CLOEXEC) != 0);
	KTM_V1_ASSERT_TRUE(ir0_open_flags_ok_for_vfs(ir0));

	ir0 = linux_open_flags_to_ir0(IR0_O_WRONLY | (int)LINUX_O_CREAT |
				     (int)LINUX_O_TRUNC);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_ACCMODE) == IR0_O_WRONLY);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_CREAT) != 0);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_TRUNC) != 0);
	KTM_V1_ASSERT_TRUE(ir0_open_flags_ok_for_vfs(ir0));

	ir0 = linux_open_flags_to_ir0(IR0_O_RDWR | (int)LINUX_O_NONBLOCK |
				     (int)LINUX_O_APPEND);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_NONBLOCK) != 0);
	KTM_V1_ASSERT_TRUE((ir0 & IR0_O_APPEND) != 0);
	KTM_V1_ASSERT_TRUE(ir0_open_flags_ok_for_vfs(ir0));

	/* Raw Linux CREAT bit must not leak as IR0-safe without translation. */
	KTM_V1_ASSERT_TRUE(!ir0_open_flags_ok_for_vfs((int)LINUX_O_CREAT));

	return KTM_OK;
}

static const ktm_scenario_t scenario_vfs_open_flags = {
	.name = "vfs.open_flags",
	.flags = 0,
	.setup = NULL,
	.run = scenario_vfs_open_flags_run,
	.teardown = NULL,
};

void ktm_scenario_register_vfs_open_flags(void)
{
	(void)ktm_scenario_register(&scenario_vfs_open_flags);
}
