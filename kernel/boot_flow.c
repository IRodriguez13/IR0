/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_flow.c
 * Description: ISA-agnostic boot sequence (policy); firmware/asm jumps to kmain.
 */

#include <ir0/boot_flow.h>
#include <ir0/boot_params.h>
#include <ir0/arch_port.h>
#include <ir0/sched.h>
#include <ir0/kernel.h>
#include "boot_init.h"

#if defined(IR0_FREESTANDING_BOOT)
void ir0_kernel_boot(const struct ir0_boot_params *params)
{
	ir0_boot_freestanding((uintptr_t)params->info);
}
#else
void ir0_kernel_boot(const struct ir0_boot_params *params)
{
	boot_early(params);
	boot_memory_serial();
	boot_drivers_rootfs();
	boot_runtime();
	boot_diagnostics();
	boot_enter_userspace();

	for (;;)
	{
		kernel_idle_poll();
		cpu_idle();
	}
}
#endif
