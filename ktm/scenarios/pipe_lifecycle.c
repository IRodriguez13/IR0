/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: pipe_lifecycle.c
 * Description: KTM scenario — pipe create/write/read/EOF/EPIPE + no fd leak.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <ir0/errno.h>
#include <ir0/pipe.h>
#include <string.h>

static int scenario_pipe_lifecycle_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	pipe_t *pipe;
	char buf[16];
	int n;
	static const char payload[] = "KTMPIPE";

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	pipe = pipe_create();
	KTM_REQUIRE(pipe != NULL);
	pipe_acquire_end(pipe, 0);
	pipe_acquire_end(pipe, 1);
	ktm_event_emit4(KTM_EVENT_INFO, KTM_SUBSYS_IPC, pipe->pipe_id, 0, 0, 0);

	n = pipe_write(pipe, payload, sizeof(payload) - 1);
	KTM_V1_ASSERT_TRUE(n == (int)(sizeof(payload) - 1));

	memset(buf, 0, sizeof(buf));
	n = pipe_read(pipe, buf, sizeof(buf));
	KTM_V1_ASSERT_TRUE(n == (int)(sizeof(payload) - 1));
	KTM_V1_ASSERT_TRUE(memcmp(buf, payload, sizeof(payload) - 1) == 0);

	/* Writer closed → reader sees EOF (0). */
	pipe_close_end(pipe, 1);
	n = pipe_read(pipe, buf, sizeof(buf));
	KTM_V1_ASSERT_TRUE(n == 0);
	pipe_close_end(pipe, 0);

	/* Fresh pipe: reader closed → writer gets -EPIPE. */
	pipe = pipe_create();
	KTM_REQUIRE(pipe != NULL);
	pipe_acquire_end(pipe, 0);
	pipe_acquire_end(pipe, 1);
	pipe_close_end(pipe, 0);
	n = pipe_write(pipe, payload, 1);
	KTM_V1_ASSERT_TRUE(n == -EPIPE);
	pipe_close_end(pipe, 1);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	(void)ktm_probe_run("proc.list", NULL);
	return KTM_OK;
}

static const ktm_scenario_t scenario_pipe_lifecycle = {
	.name = "ipc.pipe_lifecycle",
	.flags = 0,
	.setup = NULL,
	.run = scenario_pipe_lifecycle_run,
	.teardown = NULL,
};

void ktm_scenario_register_pipe_lifecycle(void)
{
	(void)ktm_scenario_register(&scenario_pipe_lifecycle);
}
