/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_net.h
 * Description: Legacy virtio-net-pci MVP probe/init.
 */

#pragma once

/** Probe and register virt0. Returns 0 or negative errno (-ENODEV if absent). */
int virtio_net_init(void);
