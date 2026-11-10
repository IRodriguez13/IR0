/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_port.c
 * Description: Default weak boot hooks — per-ISA strong definitions override.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_params.h>
#include <ir0/boot_port.h>
#include <ir0/errno.h>

#include <stdint.h>

__attribute__((weak)) void ir0_boot_port_params(
	uintptr_t firmware_handle, struct ir0_boot_params *params)
{
	params->protocol = IR0_BOOT_PROTOCOL_UNKNOWN;
	params->info = firmware_handle;
}

__attribute__((weak)) int ir0_boot_port_snapshot_fill(
	const struct ir0_boot_params *params, struct ir0_boot_snapshot *snapshot)
{
	(void)params;
	(void)snapshot;
	return -ENODEV;
}

__attribute__((weak)) void ir0_boot_port_serial_before_logging(void)
{
}

__attribute__((weak)) void ir0_boot_port_serial_after_driver_registry(void)
{
}

__attribute__((weak)) void ir0_boot_port_maybe_seed_init_from_embedded(void)
{
}
