/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_flow.h
 * Description: Portable kernel boot orchestrator — single entry for every ISA.
 */

#pragma once

struct ir0_boot_params;

/* Run the boot sequence from the typed handoff recorded by kmain(). */
void ir0_kernel_boot(const struct ir0_boot_params *params);
