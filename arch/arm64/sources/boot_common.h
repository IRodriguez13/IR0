/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — ARM64 platform setup for the portable boot path.
 */

#pragma once

#include <stdint.h>

/**
 * DTB capture, identity MMU, IRQ controller map, generic timer, VBAR and
 * optional virtio-blk. Called from boot_arch.c before ir0_kernel_boot().
 */
void arm64_platform_setup_for_portable_boot(uintptr_t firmware_handle);
