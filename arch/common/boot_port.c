/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_port.c
 * Description: Default weak boot hooks — per-ISA strong definitions override.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_port.h>

#include <stdint.h>

__attribute__((weak)) uintptr_t ir0_kmain_normalize_firmware_handle(uintptr_t firmware_handle)
{
	return firmware_handle;
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
