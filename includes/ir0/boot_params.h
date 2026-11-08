/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_params.h
 * Description: Typed boot handoff recorded from the ISA entry point.
 *
 * Each architecture translates its raw entry argument (the Multiboot info
 * pointer in RDI on x86-64, the DTB pointer in x0 on ARM64) into this
 * neutral form in ir0_boot_port_params(); kmain() records it once with
 * set_boot_params(). Code that needs the raw protocol block checks
 * @protocol before interpreting @info.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

enum ir0_boot_protocol
{
	IR0_BOOT_PROTOCOL_UNKNOWN = 0,
	IR0_BOOT_PROTOCOL_MULTIBOOT1,
	IR0_BOOT_PROTOCOL_FDT,
};

struct ir0_boot_params
{
	enum ir0_boot_protocol protocol;
	/* Physical address of the protocol block (Multiboot info, FDT blob). */
	uint64_t info;
};

/* Copy @params into the kernel's single boot record (NULL clears it). */
void set_boot_params(const struct ir0_boot_params *params);
/* The recorded handoff; protocol is UNKNOWN before set_boot_params(). */
const struct ir0_boot_params *get_boot_params(void);
