/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_net_early.h
 * Description: Minimal virtio-net freestanding smoke (features + DRIVER_OK).
 */

#pragma once

/** Probe virtio-net, print ARM64_VIRTIO_NET_OK on success. */
int arm64_virtio_net_smoke(void);
