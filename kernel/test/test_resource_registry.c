/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: test_resource_registry.c
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel - Resource registry contract
 * Drivers register IRQ/I/O-port/MMIO ranges at boot; foreach must report the
 * exact range and name, and stop when the callback asks it to.
 */

#include "test/ktest_harness.h"
#include "resource_registry.h"
#include <stddef.h>
#include <string.h>

struct ktest_res_probe {
	int found;
	int calls;
	uint8_t irq;
	uint16_t ioport_start;
	uint16_t ioport_end;
	uint64_t mmio_start;
	uint64_t mmio_end;
};

static int cb_irq(uint8_t irq, const char *name, void *ctx)
{
	struct ktest_res_probe *probe = ctx;

	probe->calls++;
	if (name && strcmp(name, "ktest_irq") == 0)
	{
		probe->found = 1;
		probe->irq = irq;
	}
	return 0;
}

static int cb_ioport(uint16_t start, uint16_t end, const char *name, void *ctx)
{
	struct ktest_res_probe *probe = ctx;

	probe->calls++;
	if (name && strcmp(name, "ktest_ioport") == 0)
	{
		probe->found = 1;
		probe->ioport_start = start;
		probe->ioport_end = end;
	}
	return 0;
}

static int cb_mmio(uint64_t start, uint64_t end, const char *name, void *ctx)
{
	struct ktest_res_probe *probe = ctx;

	probe->calls++;
	if (name && strcmp(name, "ktest_mmio") == 0)
	{
		probe->found = 1;
		probe->mmio_start = start;
		probe->mmio_end = end;
	}
	return 0;
}

static int cb_stop_first(uint8_t irq, const char *name, void *ctx)
{
	int *calls = ctx;

	(void)irq;
	(void)name;
	(*calls)++;
	return 1;
}

void ktest_resource_registry(void)
{
	struct ktest_res_probe probe;
	int stop_calls = 0;

	KTEST_BEGIN("resource_registry");

	memset(&probe, 0, sizeof(probe));
	resource_register_irq(0x5A, "ktest_irq");
	resource_foreach_irq(cb_irq, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.irq, 0x5A);
	KASSERT_GT(probe.calls, 0);

	memset(&probe, 0, sizeof(probe));
	resource_register_ioport(0x1234, 0x1237, "ktest_ioport");
	resource_foreach_ioport(cb_ioport, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.ioport_start, 0x1234);
	KASSERT_EQ(probe.ioport_end, 0x1237);
	KASSERT_GT(probe.calls, 0);

	memset(&probe, 0, sizeof(probe));
	resource_register_mmio(0xDEAD0000ULL, 0xDEAD0FFFULL, "ktest_mmio");
	resource_foreach_mmio(cb_mmio, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.mmio_start, 0xDEAD0000ULL);
	KASSERT_EQ(probe.mmio_end, 0xDEAD0FFFULL);
	KASSERT_GT(probe.calls, 0);

	/* A non-zero callback return stops the walk after the current entry. */
	resource_foreach_irq(cb_stop_first, &stop_calls);
	KASSERT_EQ(stop_calls, 1);

	/* NULL callbacks are a documented no-op, not a crash. */
	resource_foreach_irq(NULL, NULL);
	resource_foreach_ioport(NULL, NULL);
	resource_foreach_mmio(NULL, NULL);

	KTEST_END();
}
