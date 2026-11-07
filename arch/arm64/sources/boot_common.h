/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — ARM64 freestanding boot
 * Shared bring-up used by qemu-virt aggregate and board-min images.
 */

#pragma once

#include <stdint.h>

/**
 * Platform setup shared by freestanding bring-up and the portable boot_early path:
 * DTB capture, identity MMU, IRQ controller map, generic timer, VBAR, optional virtio.
 * Does not drop to EL0 or run the freestanding guest.
 */
void arm64_platform_setup_for_portable_boot(uintptr_t firmware_handle);

void arm64_early_bringup(uintptr_t firmware_handle);
