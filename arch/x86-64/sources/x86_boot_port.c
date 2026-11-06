/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: x86_boot_port.c
 * Description: x86_64 boot hooks — serial after driver registry; kmain handle passthrough.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/boot_port.h>
#include <ir0/serial_io.h>

#include <stdint.h>

uintptr_t ir0_kmain_normalize_firmware_handle(uintptr_t firmware_handle)
{
	return firmware_handle;
}

void ir0_boot_port_serial_before_logging(void)
{
}

void ir0_boot_port_serial_after_driver_registry(void)
{
	serial_init();
}

void ir0_boot_port_maybe_seed_init_from_embedded(void)
{
}
