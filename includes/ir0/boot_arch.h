/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_arch.h
 * Description: Portable boot hooks — ISA implements without #ifdef in kernel/boot_init.c.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/**
 * Run before heap and portable console: firmware handle (Multiboot, DTB, …),
 * MMU/board prep when the portable policy needs it. No-op on ISAs that fold
 * this into assembly bootstrap.
 */
void ir0_boot_arch_firmware_early(uintptr_t firmware_handle);

/**
 * Optional smoke tag immediately before init handoff / first schedule to EL0.
 * Product images use this for QEMU autokill; default is empty.
 */
void ir0_boot_arch_userspace_handoff_smoke(void);

#ifdef __cplusplus
}
#endif
