/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_net_early.h
 * Description: Minimal freestanding virtio-net device initialization.
 */

#pragma once

/** Negotiate the device and transition it to DRIVER_OK. */
int arm64_virtio_net_init(void);
