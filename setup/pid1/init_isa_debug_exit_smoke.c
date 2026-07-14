/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_isa_debug_exit_smoke.c
 * Description: Trigger platform halt → isa-debug-exit path.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/reboot.h>
#include <unistd.h>

#ifndef LINUX_REBOOT_MAGIC1
#define LINUX_REBOOT_MAGIC1 0xfee1dead
#endif
#ifndef LINUX_REBOOT_MAGIC2
#define LINUX_REBOOT_MAGIC2 672274793
#endif
#ifndef LINUX_REBOOT_CMD_HALT
#define LINUX_REBOOT_CMD_HALT 0xcdef0123
#endif

int main(void)
{
	(void)write(1, "ISA_DEBUG_EXIT_TRY\n", 19);
	/* Falls through to platform_ops.halt → outb(0xf4) + ISA_DEBUG_EXIT_OK. */
	(void)reboot(LINUX_REBOOT_CMD_HALT);
	return 1;
}
