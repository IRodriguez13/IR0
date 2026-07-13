/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: graphics_fb.c
 * Description: KTM scenario — linear framebuffer info (fbdev MVP).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/fb.h>

static int scenario_graphics_fb_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	struct ir0_fb_info info;

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	/*
	 * VGA-text fallback is not fbdev; skip asserts but keep scenario green
	 * so boot suite stays portable when Multiboot VBE is absent.
	 */
	if (!ir0_fb_is_available())
	{
		KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
		KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
		KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
		return KTM_OK;
	}

	KTM_V1_ASSERT_TRUE(ir0_fb_get_info(&info));
	KTM_V1_ASSERT_TRUE(info.width >= 320);
	KTM_V1_ASSERT_TRUE(info.height >= 200);
	KTM_V1_ASSERT_TRUE(info.bpp >= 8);
	KTM_V1_ASSERT_TRUE(info.pitch > 0);
	KTM_V1_ASSERT_TRUE(info.fb_size > 0);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_graphics_fb = {
	.name = "graphics.fb",
	.flags = 0,
	.setup = NULL,
	.run = scenario_graphics_fb_run,
	.teardown = NULL,
};

void ktm_scenario_register_graphics_fb(void)
{
	(void)ktm_scenario_register(&scenario_graphics_fb);
}
