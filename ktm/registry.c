/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: registry.c
 * Description: KTM probe registry + core init.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <config.h>
#include <ir0/serial_io.h>
#include <string.h>

#define KTM_PROBE_MAX 32

struct ktm_probe_entry
{
	const char *name;
	ktm_probe_fn fn;
	void *ctx;
	int in_use;
};

static struct ktm_probe_entry g_probes[KTM_PROBE_MAX];
static int g_inited;

void ktm_write_u64(ktm_writer_t *w, const char *key, uint64_t value)
{
	if (!w)
		return;
	ktm_transport_emit_u64("PROBE", key ? key : "?", value);
	(void)w;
}

void ktm_write_cstr(ktm_writer_t *w, const char *key, const char *value)
{
	if (!w)
		return;
	ktm_transport_emit("PROBE", key ? key : "?", value ? value : "");
	(void)w;
}

int ktm_probe_register(const char *name, ktm_probe_fn fn, void *ctx)
{
	int i;

	if (!name || !fn)
		return -1;
	for (i = 0; i < KTM_PROBE_MAX; i++)
	{
		if (!g_probes[i].in_use)
		{
			g_probes[i].name = name;
			g_probes[i].fn = fn;
			g_probes[i].ctx = ctx;
			g_probes[i].in_use = 1;
			return 0;
		}
	}
	return -1;
}

int ktm_probe_run(const char *name, ktm_writer_t *writer)
{
	int i;
	ktm_writer_t local;

	if (!name)
		return -1;
	if (!writer)
	{
		memset(&local, 0, sizeof(local));
		writer = &local;
	}
	for (i = 0; i < KTM_PROBE_MAX; i++)
	{
		if (g_probes[i].in_use && g_probes[i].name &&
		    strcmp(g_probes[i].name, name) == 0)
			return g_probes[i].fn(g_probes[i].ctx, writer);
	}
	return -1;
}

void ktm_registry_init(void)
{
	if (g_inited)
		return;
	memset(g_probes, 0, sizeof(g_probes));
	g_inited = 1;
}

void ktm_core_init(void)
{
	ktm_registry_init();
	ktm_probes_register_builtins();
	ktm_klog_init();
#if defined(CONFIG_KTM_TEST) && CONFIG_KTM_TEST
	ktm_scenarios_register_builtins();
#endif
}
