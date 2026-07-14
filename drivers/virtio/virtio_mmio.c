/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_mmio.c
 * Description: QEMU ARM virt virtio-mmio transport probe (legacy+modern magic).
 *
 * Reference: QEMU hw/arm/virt.c VIRT_MMIO @ 0x0a000000; virtio-mmio spec.
 */

#include <ir0/virtio_mmio.h>

#include <stddef.h>
#include <stdint.h>

/* Freestanding ARM boot uses pl011 from arch; weak so x64 can ignore. */
void __attribute__((weak)) pl011_puts(const char *s)
{
	(void)s;
}

int __attribute__((weak)) arm64_mmu_map_device_block(uint64_t pa)
{
	(void)pa;
	return 0;
}

static struct virtio_mmio_dev g_devs[VIRTIO_MMIO_MAX_SLOTS];
static unsigned g_ndevs;

uint32_t virtio_mmio_read(struct virtio_mmio_dev *d, uint32_t off)
{
	return d->base[off / 4U];
}

void virtio_mmio_write(struct virtio_mmio_dev *d, uint32_t off, uint32_t val)
{
	d->base[off / 4U] = val;
}

void virtio_mmio_set_status(struct virtio_mmio_dev *d, uint32_t status)
{
	virtio_mmio_write(d, VIRTIO_MMIO_REG_STATUS, status);
}

uint32_t virtio_mmio_get_status(struct virtio_mmio_dev *d)
{
	return virtio_mmio_read(d, VIRTIO_MMIO_REG_STATUS);
}

struct virtio_mmio_dev *arm64_virtio_mmio_find(uint32_t device_id)
{
	unsigned i;

	for (i = 0; i < g_ndevs; i++)
	{
		if (g_devs[i].device_id == device_id)
			return &g_devs[i];
	}
	return NULL;
}

int arm64_virtio_mmio_probe(void)
{
	unsigned i;
	uint64_t pa;

	g_ndevs = 0;

	/* One 2 MiB Device block covers the whole virtio-mmio bank. */
	if (arm64_mmu_map_device_block(VIRTIO_MMIO_BASE) != 0)
	{
		pl011_puts("ARM64_VIRTIO_MMIO_FAIL\n");
		return -1;
	}

	for (i = 0; i < VIRTIO_MMIO_MAX_SLOTS; i++)
	{
		struct virtio_mmio_dev *d;
		uint32_t magic;
		uint32_t ver;
		uint32_t id;

		pa = VIRTIO_MMIO_BASE + (uint64_t)i * VIRTIO_MMIO_STRIDE;
		d = &g_devs[g_ndevs];
		d->base = (volatile uint32_t *)(uintptr_t)pa;
		d->slot = (int)i;

		magic = virtio_mmio_read(d, VIRTIO_MMIO_REG_MAGIC);
		if (magic != VIRTIO_MMIO_MAGIC)
			continue;

		ver = virtio_mmio_read(d, VIRTIO_MMIO_REG_VERSION);
		id = virtio_mmio_read(d, VIRTIO_MMIO_REG_DEVICE_ID);
		/* Empty transport: device_id == 0. */
		if (id == 0U)
			continue;

		d->version = ver;
		d->device_id = id;
		d->vendor_id = virtio_mmio_read(d, VIRTIO_MMIO_REG_VENDOR_ID);
		g_ndevs++;
		if (g_ndevs >= VIRTIO_MMIO_MAX_SLOTS)
			break;
	}

	if (g_ndevs == 0)
	{
		pl011_puts("ARM64_VIRTIO_MMIO_FAIL\n");
		return -1;
	}

	pl011_puts("ARM64_VIRTIO_MMIO_OK\n");
	return 0;
}
