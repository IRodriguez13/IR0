/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: boot_port.h
 * Description: Portable boot hooks — weak defaults in arch/common, strong per ISA.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>

struct ir0_boot_snapshot;

#ifdef __cplusplus
extern "C" {
#endif

/* kmain firmware handle normalization (e.g. ARM64 DTB stash). Default: identity. */
uintptr_t ir0_kmain_normalize_firmware_handle(uintptr_t firmware_handle);
int ir0_boot_port_snapshot_fill(uintptr_t firmware_handle,
				struct ir0_boot_snapshot *snapshot);
int ir0_boot_port_video_init(uintptr_t firmware_handle);

void ir0_boot_port_serial_before_logging(void);
void ir0_boot_port_serial_after_driver_registry(void);
void ir0_boot_port_maybe_seed_init_from_embedded(void);
void ir0_boot_port_unmask_irqs(void);

#ifdef __cplusplus
}
#endif
