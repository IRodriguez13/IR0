/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_blk_early.h
 * Description: Virtio-blk early bring-up + ir0_block facade smoke (ARM64).
 */

#pragma once

/**
 * Probe virtio-blk, register as ir0_block device "vda", then R/W only via
 * ir0_block_read/write. Tags: ARM64_VIRTIO_BLK_OK, ARM64_BLOCKDEV_FACADE_OK.
 */
int arm64_virtio_blk_smoke(void);
