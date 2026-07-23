/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_net.c
 * Description: Minimal virtio-net-pci legacy (disable-modern=on) TX/RX MVP.
 *
 * QEMU: -netdev user,id=net0 -device virtio-net-pci,netdev=net0,disable-modern=on
 * Sources: virtio 0.9.5 legacy PCI; virtio-net header; IR0 virtio_9p vring layout.
 */

#include "virtio_net.h"

#include <config.h>
#if CONFIG_DRV_NIC_VIRTIO_NET

#include <interrupt/arch/io.h>
#include <ir0/arch_port.h>
#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/logging.h>
#include <ir0/net.h>
#include <string.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_ENABLE_BIT     0x80000000U

#define VIRTIO_VENDOR       0x1AF4
#define VIRTIO_DEV_NET      0x1000

#define VIRTIO_PCI_HOST_FEATURES 0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN     8
#define VIRTIO_PCI_QUEUE_NUM     12
#define VIRTIO_PCI_QUEUE_SEL     14
#define VIRTIO_PCI_QUEUE_NOTIFY  16
#define VIRTIO_PCI_STATUS        18
#define VIRTIO_PCI_ISR           19
#define VIRTIO_PCI_CONFIG        20

#define VIRTIO_STATUS_ACK        1
#define VIRTIO_STATUS_DRIVER     2
#define VIRTIO_STATUS_DRIVER_OK  4
#define VIRTIO_STATUS_FAILED     128

#define VIRTIO_NET_F_MAC         (1u << 5)

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* QEMU virtio-net defaults rx/tx_queue_size=256 (see -device help). */
#define VN_QSZ_MAX   256
#define VN_PKT_MAX   1518
#define VN_HDR_LEN   10
#define VN_RX_SLOTS  8

struct vring_desc
{
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct vring_used_elem
{
	uint32_t id;
	uint32_t len;
};

struct vn_queue
{
	uint16_t qsz;
	uint16_t last_used;
	struct vring_desc *desc;
	uint16_t *avail_flags;
	uint16_t *avail_idx;
	uint16_t *avail_ring;
	uint16_t *used_flags;
	uint16_t *used_idx;
	struct vring_used_elem *used_ring;
	void *mem;
};

static uint16_t g_iobase;
static int g_ready;
static uint8_t g_mac[6];
static struct vn_queue g_rx;
static struct vn_queue g_tx;
static uint8_t *g_rx_bufs[VN_RX_SLOTS];
static uint8_t *g_tx_hdr;
static uint8_t *g_tx_pkt;
static struct net_device g_dev;
static uint64_t g_rx_pkts;
static uint64_t g_tx_pkts;
static uint64_t g_tx_errs;

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

static int find_virtio_net(uint8_t *bus, uint8_t *slot)
{
	uint16_t b;
	uint8_t s;

	for (b = 0; b < 256; b++)
	{
		for (s = 0; s < 32; s++)
		{
			uint32_t id = pci_read((uint8_t)b, s, 0, 0);

			if ((id & 0xFFFF) == VIRTIO_VENDOR && (id >> 16) == VIRTIO_DEV_NET)
			{
				*bus = (uint8_t)b;
				*slot = s;
				return 0;
			}
		}
	}
	return -1;
}

static int setup_queue(struct vn_queue *q, uint16_t qsel)
{
	uint16_t qsz;
	size_t desc_sz;
	size_t avail_sz;
	size_t used_off;
	size_t need;
	uintptr_t base;

	vp_outw(VIRTIO_PCI_QUEUE_SEL, qsel);
	qsz = vp_inw(VIRTIO_PCI_QUEUE_NUM);
	if (qsz == 0 || qsz > VN_QSZ_MAX)
	{
		LOG_ERROR_FMT("VIRTIO_NET", "bad queue size qsel=%u qsz=%u (max=%u)",
			      (unsigned)qsel, (unsigned)qsz, (unsigned)VN_QSZ_MAX);
		return -EINVAL;
	}

	desc_sz = sizeof(struct vring_desc) * qsz;
	avail_sz = 6u + 2u * qsz;
	used_off = (desc_sz + avail_sz + 4095u) & ~(size_t)4095u;
	need = used_off + 6u + sizeof(struct vring_used_elem) * qsz;
	need = (need + 4095u) & ~(size_t)4095u;

	q->mem = kmalloc_aligned(need, 4096);
	if (!q->mem)
		return -ENOMEM;
	memset(q->mem, 0, need);
	base = (uintptr_t)q->mem;
	q->qsz = qsz;
	q->last_used = 0;
	q->desc = (struct vring_desc *)base;
	q->avail_flags = (uint16_t *)(base + desc_sz);
	q->avail_idx = q->avail_flags + 1;
	q->avail_ring = q->avail_idx + 1;
	q->used_flags = (uint16_t *)(base + used_off);
	q->used_idx = q->used_flags + 1;
	q->used_ring = (struct vring_used_elem *)(q->used_idx + 1);

	vp_outl(VIRTIO_PCI_QUEUE_PFN, (uint32_t)(base >> 12));
	return 0;
}

static void rx_post(uint16_t slot)
{
	uint16_t avail;
	uint8_t *buf = g_rx_bufs[slot];

	g_rx.desc[slot].addr = (uint64_t)(uintptr_t)buf;
	g_rx.desc[slot].len = VN_HDR_LEN + VN_PKT_MAX;
	g_rx.desc[slot].flags = VRING_DESC_F_WRITE;
	g_rx.desc[slot].next = 0;

	avail = *g_rx.avail_idx;
	g_rx.avail_ring[avail % g_rx.qsz] = slot;
	smp_mb();
	*g_rx.avail_idx = (uint16_t)(avail + 1);
	smp_mb();
	vp_outw(VIRTIO_PCI_QUEUE_NOTIFY, 0);
}

static void virtio_net_poll_rx(void)
{
	if (!g_ready)
		return;

	for (;;)
	{
		uint16_t u;
		uint32_t id;
		uint32_t len;
		uint8_t *buf;

		smp_mb();
		if (*g_rx.used_idx == g_rx.last_used)
			break;

		u = (uint16_t)(g_rx.last_used % g_rx.qsz);
		id = g_rx.used_ring[u].id;
		len = g_rx.used_ring[u].len;
		g_rx.last_used++;
		(void)vp_inb(VIRTIO_PCI_ISR);

		if (id >= VN_RX_SLOTS || len <= VN_HDR_LEN)
		{
			if (id < VN_RX_SLOTS)
				rx_post((uint16_t)id);
			continue;
		}

		buf = g_rx_bufs[id];
		g_rx_pkts++;
		net_receive(&g_dev, buf + VN_HDR_LEN, len - VN_HDR_LEN);
		rx_post((uint16_t)id);
	}
}

static int virtio_net_send(struct net_device *dev, void *data, size_t len)
{
	uint16_t avail;
	uint16_t d0 = 0;
	uint16_t d1 = 1;
	int spins;

	(void)dev;
	if (!g_ready || !data || len == 0 || len > VN_PKT_MAX)
		return -1;

	memset(g_tx_hdr, 0, VN_HDR_LEN);
	memcpy(g_tx_pkt, data, len);

	g_tx.desc[d0].addr = (uint64_t)(uintptr_t)g_tx_hdr;
	g_tx.desc[d0].len = VN_HDR_LEN;
	g_tx.desc[d0].flags = VRING_DESC_F_NEXT;
	g_tx.desc[d0].next = d1;

	g_tx.desc[d1].addr = (uint64_t)(uintptr_t)g_tx_pkt;
	g_tx.desc[d1].len = (uint32_t)len;
	g_tx.desc[d1].flags = 0;
	g_tx.desc[d1].next = 0;

	avail = *g_tx.avail_idx;
	g_tx.avail_ring[avail % g_tx.qsz] = d0;
	smp_mb();
	*g_tx.avail_idx = (uint16_t)(avail + 1);
	smp_mb();
	vp_outw(VIRTIO_PCI_QUEUE_NOTIFY, 1);

	for (spins = 0; spins < 2000000; spins++)
	{
		smp_mb();
		if (*g_tx.used_idx != g_tx.last_used)
		{
			g_tx.last_used++;
			(void)vp_inb(VIRTIO_PCI_ISR);
			g_tx_pkts++;
			return 0;
		}
	}
	g_tx_errs++;
	LOG_DEBUG("VIRTIO_NET", "TX notify timeout");
	return -1;
}

static void virtio_net_netdev_poll(struct net_device *dev)
{
	(void)dev;
	virtio_net_poll_rx();
}

static int virtio_net_get_irq(struct net_device *dev)
{
	(void)dev;
	return -1;
}

static int virtio_net_handle_irq(struct net_device *dev, uint8_t irq)
{
	(void)dev;
	(void)irq;
	virtio_net_poll_rx();
	return 0;
}

static void virtio_net_get_stats(struct net_device *dev, uint64_t *rx_pkts,
				 uint64_t *tx_pkts, uint64_t *rx_errs, uint64_t *tx_errs)
{
	(void)dev;
	if (rx_pkts)
		*rx_pkts = g_rx_pkts;
	if (tx_pkts)
		*tx_pkts = g_tx_pkts;
	if (rx_errs)
		*rx_errs = 0;
	if (tx_errs)
		*tx_errs = g_tx_errs;
}

int virtio_net_init(void)
{
	uint8_t bus, slot;
	uint32_t bar0;
	uint32_t cmd;
	uint32_t host_feat;
	int i;
	int rc;

	g_ready = 0;
	if (find_virtio_net(&bus, &slot) != 0)
	{
		LOG_INFO("VIRTIO_NET", "device absent");
		return -ENODEV;
	}

	cmd = pci_read(bus, slot, 0, 0x04);
	pci_write(bus, slot, 0, 0x04, cmd | 0x5);

	bar0 = pci_read(bus, slot, 0, 0x10);
	if (!(bar0 & 1))
	{
		LOG_ERROR_FMT("VIRTIO_NET", "BAR0 not I/O space bar0=0x%x", bar0);
		return -ENODEV;
	}
	g_iobase = (uint16_t)(bar0 & ~0x3u);

	vp_outb(VIRTIO_PCI_STATUS, 0);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

	host_feat = vp_inl(VIRTIO_PCI_HOST_FEATURES);
	vp_outl(VIRTIO_PCI_GUEST_FEATURES, host_feat & VIRTIO_NET_F_MAC);

	for (i = 0; i < 6; i++)
		g_mac[i] = vp_inb((uint16_t)(VIRTIO_PCI_CONFIG + i));

	rc = setup_queue(&g_rx, 0);
	if (rc < 0)
		goto fail;
	rc = setup_queue(&g_tx, 1);
	if (rc < 0)
		goto fail;

	g_tx_hdr = (uint8_t *)kmalloc_aligned(VN_HDR_LEN, 16);
	g_tx_pkt = (uint8_t *)kmalloc_aligned(VN_PKT_MAX, 16);
	if (!g_tx_hdr || !g_tx_pkt)
		goto fail;

	for (i = 0; i < VN_RX_SLOTS; i++)
	{
		g_rx_bufs[i] = (uint8_t *)kmalloc_aligned(VN_HDR_LEN + VN_PKT_MAX, 16);
		if (!g_rx_bufs[i])
			goto fail;
		memset(g_rx_bufs[i], 0, VN_HDR_LEN + VN_PKT_MAX);
	}

	vp_outb(VIRTIO_PCI_STATUS,
		VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

	for (i = 0; i < VN_RX_SLOTS && i < g_rx.qsz; i++)
		rx_post((uint16_t)i);

	memset(&g_dev, 0, sizeof(g_dev));
	g_dev.name = "virt0";
	memcpy(g_dev.mac, g_mac, 6);
	g_dev.mtu = 1500;
	g_dev.flags = IFF_UP | IFF_RUNNING | IFF_BROADCAST;
	g_dev.send = virtio_net_send;
	g_dev.poll = virtio_net_netdev_poll;
	g_dev.get_irq_line = virtio_net_get_irq;
	g_dev.handle_irq = virtio_net_handle_irq;
	g_dev.get_stats = virtio_net_get_stats;
	net_register_device(&g_dev);

	g_ready = 1;
	LOG_INFO_FMT("VIRTIO_NET", "ready MAC %02x:%02x:%02x:%02x:%02x:%02x",
		     g_mac[0], g_mac[1], g_mac[2], g_mac[3], g_mac[4], g_mac[5]);
	return 0;

fail:
	LOG_ERROR("VIRTIO_NET", "init failed");
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
	return rc < 0 ? rc : -ENOMEM;
}

#else /* !CONFIG_DRV_NIC_VIRTIO_NET */

int virtio_net_init(void)
{
	return -ENODEV;
}

#endif
