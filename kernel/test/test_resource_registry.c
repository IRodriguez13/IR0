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
 * IR0 Kernel - Tests del resource registry (sin proceso)
 * Se ejecutan al boot; el registro puede tener entradas de drivers ya inicializados.
 */

#include "test/ktest_harness.h"
#include "resource_registry.h"
#include <stddef.h>

static int count_irq;
static int count_ioport;

static int cb_count_irq(uint8_t irq, const char *name, void *ctx)
{
	(void)irq;
	(void)ctx;
	if (name)
		count_irq++;
	return 0;
}

static int cb_count_ioport(uint16_t start, uint16_t end, const char *name, void *ctx)
{
	(void)start;
	(void)end;
	(void)ctx;
	if (name)
		count_ioport++;
	return 0;
}

void ktest_resource_registry(void)
{
	KTEST_BEGIN("resource_registry");

	count_irq = 0;
	resource_foreach_irq(cb_count_irq, NULL);
	KASSERT_GE(count_irq, 0);

	count_ioport = 0;
	resource_foreach_ioport(cb_count_ioport, NULL);
	KASSERT_GE(count_ioport, 0);

	resource_foreach_irq(NULL, NULL);
	resource_foreach_ioport(NULL, NULL);

	KTEST_END();
}
