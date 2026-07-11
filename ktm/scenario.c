/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: scenario.c
 * Description: Scenario registry and runner.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ktm_internal.h"
#include <config.h>
#include <string.h>

#define KTM_SCENARIO_MAX 16

static const ktm_scenario_t *g_scenarios[KTM_SCENARIO_MAX];
static unsigned g_nsc;
static unsigned g_suite_pass;
static unsigned g_suite_fail;

unsigned ktm_suite_pass_count(void)
{
	return g_suite_pass;
}

unsigned ktm_suite_fail_count(void)
{
	return g_suite_fail;
}

void ktm_suite_reset(void)
{
	g_suite_pass = 0;
	g_suite_fail = 0;
	ktm_event_ring_reset_cursor();
}

int ktm_scenario_register(const ktm_scenario_t *sc)
{
	if (!sc || !sc->name || !sc->run || g_nsc >= KTM_SCENARIO_MAX)
		return -1;
	g_scenarios[g_nsc++] = sc;
	return 0;
}

int ktm_scenario_run(const char *name)
{
	unsigned i;
	ktm_context_t ctx;
	int ret = -1;
	const ktm_scenario_t *sc = NULL;

	if (!name)
		return -1;
	for (i = 0; i < g_nsc; i++)
	{
		if (g_scenarios[i] && strcmp(g_scenarios[i]->name, name) == 0)
		{
			sc = g_scenarios[i];
			break;
		}
	}
	if (!sc)
	{
		ktm_transport_emit("TEST_END", name, "MISSING");
		g_suite_fail++;
		return -1;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.current_name = sc->name;
	ktm_set_current_context(&ctx);

	ktm_event_emit4(KTM_EVENT_TEST_BEGIN, KTM_SUBSYS_TEST, 0, 0, 0, 0);
	ktm_transport_emit("TEST_BEGIN", sc->name, NULL);

	if (sc->setup)
		(void)sc->setup(&ctx);
	ret = sc->run(&ctx);
	if (sc->teardown)
		sc->teardown(&ctx);

	if (ret != KTM_OK || ctx.asserts_fail > 0)
	{
		ktm_transport_emit("TEST_END", sc->name, "FAIL");
		g_suite_fail++;
		ret = -1;
	}
	else
	{
		ktm_transport_emit("TEST_END", sc->name, "PASS");
		g_suite_pass++;
		ret = KTM_OK;
	}

	ktm_event_emit4(KTM_EVENT_TEST_END, KTM_SUBSYS_TEST, (uint64_t)(ret == 0), 0, 0, 0);
	ktm_set_current_context(NULL);
	return ret;
}

void ktm_scenarios_run_boot(void)
{
#if defined(CONFIG_KTM_TEST) && CONFIG_KTM_TEST
	static const char *const boot_suite[] = {
		"process.lifecycle",
		"ipc.pipe_lifecycle",
		"mm.cow_fork",
		"mm.vma",
		"mm.page_tables",
		"mm.steady_state",
		"vfs.devfs",
		"shell.redir",
		"mm.oom_class",
		"process.wait_drain",
		"graphics.fb",
		"input.events0",
		"vfs.open_flags",
		"process.exec",
		"process.fork_rollback",
	};
	unsigned i;

	g_suite_pass = 0;
	g_suite_fail = 0;
	for (i = 0; i < sizeof(boot_suite) / sizeof(boot_suite[0]); i++)
		(void)ktm_scenario_run(boot_suite[i]);
	ktm_transport_suite_end(g_suite_pass, g_suite_fail);
#else
	(void)0;
#endif
}
