/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel_wiring.c
 * Description: Portable kmain → ir0_kernel_boot (ISA entry lives under arch/).
 */

#include <stdint.h>

#include <ir0/boot_flow.h>
#include <ir0/boot_params.h>
#include <ir0/boot_port.h>
#include <ir0/kernel.h>

void kmain(uintptr_t firmware_handle)
{
	struct ir0_boot_params params = { 0 };

	ir0_boot_port_params(firmware_handle, &params);
	set_boot_params(&params);
	ir0_kernel_boot(get_boot_params());
}
