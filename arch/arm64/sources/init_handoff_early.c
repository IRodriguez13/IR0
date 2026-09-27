/* SPDX-License-Identifier: GPL-3.0-only */
#include "elf_load_early.h"
#include "rootfs_early.h"

#include <ir0/boot_log.h>
#include <ir0/init_handoff.h>

static int prepare_rootfs(void *context)
{
	(void)context;
	arm64_rootfs_early_init();
	return 0;
}

static int prepare_init_task(void *context)
{
	(void)context;
	return 0;
}

static long load_init(void *context, const char *path, char *const argv[])
{
	(void)context;
	(void)argv;
	if (!path || path[0] != '/' || path[1] != 'i' || path[2] != 'n' ||
	    path[3] != 'i' || path[4] != 't' || path[5] != '\0')
		return -1;
	if (arm64_busybox_prepare() != 0)
		return -1;
	return 1;
}

static void init_loaded(void *context, long init_pid)
{
	(void)context;
	if (init_pid == 1)
		ir0_boot_smoke("ARM64_PID1_LOAD_OK");
}

static void attach_console(void *context)
{
	(void)context;
	ir0_boot_smoke("ARM64_INIT_HANDOFF_OK");
}

static void schedule_init(void *context)
{
	(void)context;
	arm64_busybox_enter();
}

int arm64_init_handoff_early(void)
{
	char *argv[] = { "/init", "echo", "ARM64_BUSYBOX_EL0_OK", 0 };
	static const struct ir0_init_handoff_ops ops = {
		.prepare_rootfs = prepare_rootfs,
		.prepare_init_task = prepare_init_task,
		.load_init = load_init,
		.init_loaded = init_loaded,
		.spawn_idle = 0,
		.attach_console = attach_console,
		.schedule = schedule_init,
	};

	return ir0_init_handoff_run(&ops, 0, "/init", argv) < 0 ? -1 : 0;
}
