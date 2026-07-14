/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_net_early.c
 * Description: Freestanding virtio-net: bring-up + MAC read (no full TCP stack).
 */

#include "virtio_net_early.h"
#include "pl011.h"

#include <ir0/virtio_mmio.h>

#include <stdint.h>

int arm64_virtio_net_smoke(void)
{
	struct virtio_mmio_dev *d;
	uint32_t status;
	uint8_t mac[6];
	unsigned i;

	d = arm64_virtio_mmio_find(VIRTIO_ID_NET);
	if (!d)
	{
		pl011_puts("ARM64_VIRTIO_NET_FAIL\n");
		return -1;
	}

	virtio_mmio_set_status(d, 0);
	virtio_mmio_set_status(d, VIRTIO_STATUS_ACKNOWLEDGE);
	virtio_mmio_set_status(d, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);

	/* Modern: VIRTIO_F_VERSION_1 (bit 32) + VIRTIO_NET_F_MAC (bit 5). */
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 1);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES, 1U);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 0);
	virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES, (1U << 5));

	if (d->version >= 2U)
	{
		status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
			 VIRTIO_STATUS_FEATURES_OK;
		virtio_mmio_set_status(d, status);
		if ((virtio_mmio_get_status(d) & VIRTIO_STATUS_FEATURES_OK) == 0)
		{
			/* Retry: VERSION_1 only (no MAC). */
			virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 1);
			virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES, 1U);
			virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL, 0);
			virtio_mmio_write(d, VIRTIO_MMIO_REG_DRIVER_FEATURES, 0);
			virtio_mmio_set_status(d, status);
			if ((virtio_mmio_get_status(d) & VIRTIO_STATUS_FEATURES_OK) == 0)
			{
				pl011_puts("ARM64_VIRTIO_NET_FAIL\n");
				return -1;
			}
		}
	}

	{
		volatile uint8_t *cfg =
			(volatile uint8_t *)&d->base[VIRTIO_MMIO_REG_CONFIG / 4U];

		for (i = 0; i < 6; i++)
			mac[i] = cfg[i];
	}

	status = VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER |
		 VIRTIO_STATUS_FEATURES_OK | VIRTIO_STATUS_DRIVER_OK;
	virtio_mmio_set_status(d, status);

	/* Non-zero MAC or zeros both OK if DRIVER_OK stuck. */
	if ((virtio_mmio_get_status(d) & VIRTIO_STATUS_DRIVER_OK) == 0)
	{
		pl011_puts("ARM64_VIRTIO_NET_FAIL\n");
		return -1;
	}

	(void)mac;
	pl011_puts("ARM64_VIRTIO_NET_OK\n");
	return 0;
}
