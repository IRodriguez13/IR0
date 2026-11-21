/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.h
 * Description: Portable boot hooks — ISA implements without #ifdef in kernel/boot/boot_init.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct ir0_boot_params;
struct driver_boot_table;

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Run before heap and portable console with the typed boot handoff
 * (Multiboot, DTB, …): MMU/board prep when the portable policy needs it.
 * No-op on ISAs that fold this into assembly bootstrap.
 */
void ir0_boot_arch_firmware_early(const struct ir0_boot_params *params);

/**
 * Optional smoke tag immediately before init handoff / first schedule to EL0.
 * Product images use this for QEMU autokill; x86-64 leaves it empty.
 */
void ir0_boot_arch_userspace_handoff_smoke(void);

/**
 * ir0_boot_arch_driver_table - the platform's boot driver table
 * (<ir0/driver_bootstrap.h>). Every ISA defines it (PC:
 * arch/x86-64/sources/boot_drivers.c; arm64: arch/arm64/sources/boot_arch.c).
 */
extern const struct driver_boot_table ir0_boot_arch_driver_table;

#ifdef __cplusplus
}
#endif
