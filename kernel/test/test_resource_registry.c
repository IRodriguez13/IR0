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
#include "syscalls.h"
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

struct ktest_name_probe {
	int found;
	char name[RESOURCE_NAME_MAX];
};

static int cb_irq_name(uint8_t irq, const char *name, void *ctx)
{
	struct ktest_name_probe *probe = ctx;

	if (irq == 0x5B && name)
	{
		probe->found = 1;
		strncpy(probe->name, name, sizeof(probe->name) - 1);
	}
	return 0;
}

void ktest_resource_registry(void)
{
	struct ktest_res_probe probe;
	int stop_calls = 0;

	KTEST_BEGIN("resource_registry");

	/*
	 * Register, verify, and unregister each resource.  The registry is a
	 * boot-wide table; leaving test entries behind would leak them into
	 * /proc/interrupts, /proc/ioports, and /proc/iomem for every later
	 * test and for userspace.
	 */
	resource_register_irq(0x5A, "ktest_irq");
	memset(&probe, 0, sizeof(probe));
	resource_foreach_irq(cb_irq, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.irq, 0x5A);
	KASSERT_GT(probe.calls, 0);

	/* A non-zero callback return stops the walk after the current entry. */
	resource_foreach_irq(cb_stop_first, &stop_calls);
	KASSERT_EQ(stop_calls, 1);

	/* Wrong name or range must not remove the entry. */
	KASSERT_EQ(resource_unregister_irq(0x5A, "ktest_other"), -ENOENT);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_irq(cb_irq, &probe);
	KASSERT(probe.found);

	KASSERT_EQ(resource_unregister_irq(0x5A, "ktest_irq"), 0);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_irq(cb_irq, &probe);
	KASSERT(!probe.found);
	KASSERT_EQ(resource_unregister_irq(0x5A, "ktest_irq"), -ENOENT);

	/*
	 * Names are copied at registration.  Keeping the caller's pointer
	 * would make the registry dangle as soon as the caller reused or
	 * released its buffer; mutate the source and require the original.
	 */
	{
		char mutable_name[RESOURCE_NAME_MAX];
		struct ktest_name_probe name_probe;

		strcpy(mutable_name, "ktest_copy");
		resource_register_irq(0x5B, mutable_name);
		strcpy(mutable_name, "mutated");
		memset(&name_probe, 0, sizeof(name_probe));
		resource_foreach_irq(cb_irq_name, &name_probe);
		KASSERT(name_probe.found);
		KASSERT(strcmp(name_probe.name, "ktest_copy") == 0);
		KASSERT_EQ(resource_unregister_irq(0x5B, "ktest_copy"), 0);
	}

	resource_register_ioport(0x1234, 0x1237, "ktest_ioport");
	memset(&probe, 0, sizeof(probe));
	resource_foreach_ioport(cb_ioport, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.ioport_start, 0x1234);
	KASSERT_EQ(probe.ioport_end, 0x1237);
	KASSERT_GT(probe.calls, 0);

	KASSERT_EQ(resource_unregister_ioport(0x1234, 0x1236,
					      "ktest_ioport"), -ENOENT);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_ioport(cb_ioport, &probe);
	KASSERT(probe.found);

	KASSERT_EQ(resource_unregister_ioport(0x1234, 0x1237,
					      "ktest_ioport"), 0);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_ioport(cb_ioport, &probe);
	KASSERT(!probe.found);

	resource_register_mmio(0xDEAD0000ULL, 0xDEAD0FFFULL, "ktest_mmio");
	memset(&probe, 0, sizeof(probe));
	resource_foreach_mmio(cb_mmio, &probe);
	KASSERT(probe.found);
	KASSERT_EQ(probe.mmio_start, 0xDEAD0000ULL);
	KASSERT_EQ(probe.mmio_end, 0xDEAD0FFFULL);
	KASSERT_GT(probe.calls, 0);

	KASSERT_EQ(resource_unregister_mmio(0xDEAD0000ULL, 0xDEAD0FFEULL,
					    "ktest_mmio"), -ENOENT);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_mmio(cb_mmio, &probe);
	KASSERT(probe.found);

	KASSERT_EQ(resource_unregister_mmio(0xDEAD0000ULL, 0xDEAD0FFFULL,
					    "ktest_mmio"), 0);
	memset(&probe, 0, sizeof(probe));
	resource_foreach_mmio(cb_mmio, &probe);
	KASSERT(!probe.found);

	/* NULL callbacks are a documented no-op, not a crash. */
	resource_foreach_irq(NULL, NULL);
	resource_foreach_ioport(NULL, NULL);
	resource_foreach_mmio(NULL, NULL);

	KTEST_END();
}

static int ktest_proc_contains(const char *path, const char *needle)
{
	char buf[2048];
	int64_t fd;
	int64_t n;

	fd = sys_open(path, 0, 0);
	if (fd <= 0)
		return -1;
	memset(buf, 0, sizeof(buf));
	n = sys_read((int)fd, buf, sizeof(buf) - 1);
	sys_close((int)fd);
	if (n <= 0)
		return -1;
	return strstr(buf, needle) != NULL;
}

void ktest_resource_procfs_visibility(void)
{
	KTEST_BEGIN("resource_procfs_visibility");

	resource_register_irq(0x7E, "ktest_proc_irq");
	resource_register_ioport(0x2F00, 0x2F03, "ktest_proc_ioport");
	resource_register_mmio(0xCAFE0000ULL, 0xCAFE0FFFULL,
			       "ktest_proc_mmio");

	/* Each reader must expose the registered resource in its own format. */
	KASSERT_EQ(ktest_proc_contains("/proc/interrupts",
				       "126\tktest_proc_irq"), 1);
	KASSERT_EQ(ktest_proc_contains("/proc/ioports",
				       "12032\t12035\tktest_proc_ioport"), 1);
	KASSERT_EQ(ktest_proc_contains(
			   "/proc/iomem",
			   "00000000cafe0000-00000000cafe0fff : ktest_proc_mmio"),
		   1);

	KASSERT_EQ(resource_unregister_irq(0x7E, "ktest_proc_irq"), 0);
	KASSERT_EQ(resource_unregister_ioport(0x2F00, 0x2F03,
					      "ktest_proc_ioport"), 0);
	KASSERT_EQ(resource_unregister_mmio(0xCAFE0000ULL, 0xCAFE0FFFULL,
					    "ktest_proc_mmio"), 0);

	/* Unregistered resources must disappear from the exported views. */
	KASSERT_EQ(ktest_proc_contains("/proc/interrupts",
				       "ktest_proc_irq"), 0);
	KASSERT_EQ(ktest_proc_contains("/proc/ioports",
				       "ktest_proc_ioport"), 0);
	KASSERT_EQ(ktest_proc_contains("/proc/iomem", "ktest_proc_mmio"), 0);

	KTEST_END();
}
