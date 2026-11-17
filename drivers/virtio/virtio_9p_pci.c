/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_9p_pci.c
 * Description: Legacy virtio-pci transport for the 9P client (PC platform):
 *              PCI configuration mechanism #1 and the device's I/O BAR.
 *
 * Sources: virtio 0.9.5 legacy PCI.
 */

#include "virtio_9p.h"

#include <ir0/arch_io.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_ENABLE_BIT     0x80000000U

#define VIRTIO_VENDOR      0x1AF4
#define VIRTIO_DEV_9P      0x1009

#define VIRTIO_PCI_HOST_FEATURES 0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN     8
#define VIRTIO_PCI_QUEUE_NUM     12
#define VIRTIO_PCI_QUEUE_SEL     14
#define VIRTIO_PCI_QUEUE_NOTIFY  16
#define VIRTIO_PCI_STATUS        18
#define VIRTIO_PCI_ISR           19

#define VIRTIO_STATUS_ACK        1
#define VIRTIO_STATUS_DRIVER     2
#define VIRTIO_STATUS_DRIVER_OK  4
#define VIRTIO_STATUS_FAILED     128

static uint16_t g_iobase;

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	outl(PCI_CONFIG_DATA, val);
}

static void vp_outb(uint16_t off, uint8_t v)
{
	outb((uint16_t)(g_iobase + off), v);
}

static void vp_outw(uint16_t off, uint16_t v)
{
	outw((uint16_t)(g_iobase + off), v);
}

static void vp_outl(uint16_t off, uint32_t v)
{
	outl((uint16_t)(g_iobase + off), v);
}

static uint8_t vp_inb(uint16_t off)
{
	return inb((uint16_t)(g_iobase + off));
}

static uint16_t vp_inw(uint16_t off)
{
	return inw((uint16_t)(g_iobase + off));
}

static uint32_t vp_inl(uint16_t off)
{
	return inl((uint16_t)(g_iobase + off));
}

static void virtio_9p_pci_notify(void)
{
	vp_outw(VIRTIO_PCI_QUEUE_NOTIFY, 0);
}

static void virtio_9p_pci_ack_interrupt(void)
{
	(void)vp_inb(VIRTIO_PCI_ISR);
}

static const struct virtio_9p_transport virtio_9p_pci_transport = {
	.notify = virtio_9p_pci_notify,
	.ack_interrupt = virtio_9p_pci_ack_interrupt,
};

static int find_virtio_9p(uint8_t *bus, uint8_t *slot)
{
	uint16_t b;
	uint8_t s;

	for (b = 0; b < 256; b++)
	{
		for (s = 0; s < 32; s++)
		{
			uint32_t id = pci_read((uint8_t)b, s, 0, 0);
			if ((id & 0xFFFF) == VIRTIO_VENDOR && (id >> 16) == VIRTIO_DEV_9P)
			{
				*bus = (uint8_t)b;
				*slot = s;
				return 0;
			}
		}
	}
	return -1;
}

int virtio_9p_pci_probe(void)
{
	uint8_t bus, slot;
	uint32_t bar0;
	uint32_t cmd;
	uint16_t qsz;
	uintptr_t pfn;
	int rc;

	if (find_virtio_9p(&bus, &slot) != 0)
	{
		klog_smoke("HOSTSHARE_9P_ABSENT");
		return -ENODEV;
	}

	cmd = pci_read(bus, slot, 0, 0x04);
	pci_write(bus, slot, 0, 0x04, cmd | 0x5); /* IO + BusMaster */

	bar0 = pci_read(bus, slot, 0, 0x10);
	if (!(bar0 & 1))
	{
		klog_smoke("HOSTSHARE_9P_NO_IOBAR");
		return -ENODEV;
	}
	g_iobase = (uint16_t)(bar0 & ~0x3u);

	vp_outb(VIRTIO_PCI_STATUS, 0);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

	(void)vp_inl(VIRTIO_PCI_HOST_FEATURES);
	vp_outl(VIRTIO_PCI_GUEST_FEATURES, 0);

	vp_outw(VIRTIO_PCI_QUEUE_SEL, 0);
	qsz = vp_inw(VIRTIO_PCI_QUEUE_NUM);
	rc = virtio_9p_setup_queue(qsz, &pfn);
	if (rc < 0)
	{
		if (rc == -EINVAL)
			klog_smoke("HOSTSHARE_9P_BAD_QSZ");
		vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
		return rc;
	}
	vp_outl(VIRTIO_PCI_QUEUE_PFN, (uint32_t)pfn);

	vp_outb(VIRTIO_PCI_STATUS,
		VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

	return virtio_9p_start(&virtio_9p_pci_transport);
}
