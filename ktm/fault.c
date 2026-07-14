/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fault.c
 * Description: Fault injection stub (CONFIG_KTM_FAULT).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ktm_internal.h>
#include <config.h>
#include <string.h>

#if defined(CONFIG_KTM_FAULT) && CONFIG_KTM_FAULT

#define KTM_FAULT_MAX 8

struct ktm_fault_slot
{
	char name[32];
	ktm_fault_mode_t mode;
	uint32_t value;
	uint32_t hits;
	int armed;
};

static struct ktm_fault_slot g_faults[KTM_FAULT_MAX];
static uint32_t g_seed = 1;

void ktm_fault_seed(uint32_t seed)
{
	g_seed = seed ? seed : 1;
}

int ktm_fault_configure(const char *name, ktm_fault_mode_t mode, uint32_t value)
{
	int i;

	if (!name)
		return -1;
	for (i = 0; i < KTM_FAULT_MAX; i++)
	{
		if (!g_faults[i].armed || strcmp(g_faults[i].name, name) == 0)
		{
			memset(&g_faults[i], 0, sizeof(g_faults[i]));
			strncpy(g_faults[i].name, name, sizeof(g_faults[i].name) - 1);
			g_faults[i].mode = mode;
			g_faults[i].value = value;
			g_faults[i].armed = 1;
			return 0;
		}
	}
	return -1;
}

bool ktm_fault_should_fail(const char *name)
{
	int i;

	if (!name)
		return false;
	for (i = 0; i < KTM_FAULT_MAX; i++)
	{
		if (!g_faults[i].armed || strcmp(g_faults[i].name, name) != 0)
			continue;
		g_faults[i].hits++;
		switch (g_faults[i].mode)
		{
		case KTM_FAULT_ONCE:
			if (g_faults[i].hits == 1)
			{
				ktm_event_emit4(KTM_EVENT_FAULT_INJECTED, KTM_SUBSYS_CORE,
						0, 0, 0, 0);
				ktm_transport_emit("FAULT_INJECTED", name, "once");
				g_faults[i].armed = 0;
				return true;
			}
			return false;
		case KTM_FAULT_AFTER_N:
			if (g_faults[i].hits > g_faults[i].value)
			{
				ktm_event_emit4(KTM_EVENT_FAULT_INJECTED, KTM_SUBSYS_CORE,
						0, 0, 0, 0);
				ktm_transport_emit("FAULT_INJECTED", name, "after_n");
				return true;
			}
			return false;
		case KTM_FAULT_EVERY_N:
			if (g_faults[i].value == 0)
				return false;
			if ((g_faults[i].hits % g_faults[i].value) == 0)
			{
				ktm_event_emit4(KTM_EVENT_FAULT_INJECTED, KTM_SUBSYS_CORE,
						0, 0, 0, 0);
				ktm_transport_emit("FAULT_INJECTED", name, "every_n");
				return true;
			}
			return false;
		case KTM_FAULT_PROBABILITY:
			/* value = percent 1..100; LCG using seed. */
			{
				uint32_t r;

				g_seed = g_seed * 1664525u + 1013904223u;
				r = (g_seed >> 16) % 100u;
				if (r < g_faults[i].value && g_faults[i].value > 0)
				{
					ktm_event_emit4(KTM_EVENT_FAULT_INJECTED,
							KTM_SUBSYS_CORE, 0, 0, 0, 0);
					ktm_transport_emit("FAULT_INJECTED", name, "prob");
					return true;
				}
			}
			return false;
		default:
			return false;
		}
	}
	return false;
}

#else

void ktm_fault_seed(uint32_t seed)
{
	(void)seed;
}

int ktm_fault_configure(const char *name, ktm_fault_mode_t mode, uint32_t value)
{
	(void)name;
	(void)mode;
	(void)value;
	return -1;
}

bool ktm_fault_should_fail(const char *name)
{
	(void)name;
	return false;
}

#endif
