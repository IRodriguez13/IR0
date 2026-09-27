/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_blk_early.h
 * Description: Virtio-blk early bring-up behind the portable block facade.
 */

#pragma once

/** Probe virtio-blk and register it as the portable block device "vda". */
int arm64_virtio_blk_init(void);

/** Exercise registered block I/O through ir0_block_* only. */
int arm64_virtio_blk_facade_probe(void);
