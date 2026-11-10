/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_flow.h
 * Description: Portable kernel boot orchestrator — single entry for every ISA.
 */

#pragma once

#include <stdint.h>

struct ir0_boot_params;

/* Run the boot sequence from the typed handoff recorded by kmain(). */
void ir0_kernel_boot(const struct ir0_boot_params *params);

/*
 * When IR0_FREESTANDING_BOOT: portable policy is ir0_kernel_boot() →
 * ir0_boot_freestanding(); each ISA provides the symbol in its entry /
 * bring-up TU (no arch_* prefix in portable kernel/).
 */
void ir0_boot_freestanding(uintptr_t firmware_handle);
