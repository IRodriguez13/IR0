/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: shell_redir.c
 * Description: KTM scenario — pipe as shell redirect stand-in.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "../ktm_internal.h"
#include <ir0/errno.h>
#include <ir0/pipe.h>
#include <string.h>

static int scenario_shell_redir_run(ktm_context_t *ctx)
{
	ktm_system_snapshot_t before;
	ktm_system_snapshot_t after;
	pipe_t *pipe;
	char buf[32];
	int n;
	static const char line[] = "echo hi\n";

	(void)ctx;
	KTM_REQUIRE(ktm_snapshot_take(&before) == 0);

	pipe = pipe_create();
	KTM_REQUIRE(pipe != NULL);
	pipe_acquire_end(pipe, 0);
	pipe_acquire_end(pipe, 1);

	/* Shell-style: write to "stdout" pipe end, read redirected input. */
	n = pipe_write(pipe, line, sizeof(line) - 1);
	KTM_V1_ASSERT_TRUE(n == (int)(sizeof(line) - 1));
	memset(buf, 0, sizeof(buf));
	n = pipe_read(pipe, buf, sizeof(buf));
	KTM_V1_ASSERT_TRUE(n == (int)(sizeof(line) - 1));
	KTM_V1_ASSERT_TRUE(memcmp(buf, line, sizeof(line) - 1) == 0);

	pipe_close_end(pipe, 1);
	pipe_close_end(pipe, 0);

	KTM_REQUIRE(ktm_snapshot_take(&after) == 0);
	KTM_ASSERT_NO_PROCESS_LEAK(&before, &after);
	KTM_ASSERT_NO_FRAME_LEAK(&before, &after);
	return KTM_OK;
}

static const ktm_scenario_t scenario_shell_redir = {
	.name = "shell.redir",
	.flags = 0,
	.setup = NULL,
	.run = scenario_shell_redir_run,
	.teardown = NULL,
};

void ktm_scenario_register_shell_redir(void)
{
	(void)ktm_scenario_register(&scenario_shell_redir);
}
