/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — virtio-blk
 *
 * Registers the first virtio-mmio block device as the portable block
 * device "vda". The transport probe lives in virtio_mmio.h.
 */

#pragma once

/** Bring the device up and register it. 0 on success, -1 or -errno. */
int virtio_blk_init(void);

/**
 * Read back a pattern written to the last sector through ir0_block_*.
 * Debug boot only. 0 when the bytes match.
 */
int virtio_blk_facade_probe(void);
