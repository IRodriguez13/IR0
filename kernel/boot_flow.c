/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_flow.c
 * Description: ISA-agnostic boot sequence (policy); firmware/asm jumps to kmain.
 */

#include <ir0/boot_flow.h>

#include <stdint.h>

#if defined(IR0_FREESTANDING_BOOT)
void ir0_kernel_boot(uintptr_t firmware_handle)
{
	ir0_boot_freestanding(firmware_handle);
}
#else

#include <ir0/arch_port.h>
#include <ir0/sched.h>
#include <ir0/kernel.h>

#include "boot_init.h"

void ir0_kernel_boot(uintptr_t firmware_handle)
{
	boot_early(firmware_handle);
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
