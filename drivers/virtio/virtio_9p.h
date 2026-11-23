/* SPDX-License-Identifier: GPL-3.0-only */

/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_9p.h
 * Description: 9P2000.L client over a legacy virtio queue (impl header):
 *              the interface between the client and its transport.
 */

#pragma once

#include <ir0/virtio_9p.h>
#include <stdint.h>

/* How the client reaches the device once its queue 0 is live. */
struct virtio_9p_transport
{
	/* Tell the device that queue 0 has a new request. */
	void (*notify)(void);
	/* Read (and so clear) the device's interrupt status. */
	void (*ack_interrupt)(void);
};

/*
 * Allocate the legacy vring for qsz entries and the request/response
 * buffers; store the descriptor table's page frame in *pfn. -EINVAL for a
 * queue size the client cannot use, -ENOMEM when allocation fails.
 */
int virtio_9p_setup_queue(uint16_t qsz, uintptr_t *pfn);

/* The device is DRIVER_OK: negotiate 9P2000.L and attach the share root. */
int virtio_9p_start(const struct virtio_9p_transport *transport);

/* PC: find the virtio-9p PCI device, set it up and start the client. */
int virtio_9p_pci_probe(void);
