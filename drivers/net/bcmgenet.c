/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bcmgenet.c
 * Description: BCM2711 GENET v5 driver (RPi4). No Linux sk_buff/NAPI copy.
 *
 * References:
 *   Linux drivers/net/ethernet/broadcom/genet/bcmgenet.c
 *   BCM2711 DMA burst length 0x08 (netdev 2019-11-11)
 */

#include "bcmgenet.h"

#include <config.h>
#include <arm64_board.h>
#include <ir0/dma.h>
#include <ir0/errno.h>
#include <ir0/irq.h>
#include <ir0/ktm/klog.h>
#include <ir0/netdevice.h>
#include <string.h>

struct bcmgenet_priv
{
	volatile uint32_t *base;
	uintptr_t mmio_pa;
	size_t mmio_size;
	uint32_t irq[2];
	uint32_t irq_count;
	uint8_t mac[6];
	uint32_t phy_addr;
	int rgmii_rxid;
	int link_up;
	void *rx_buf;
	uintptr_t rx_phys;
	void *tx_buf;
	uintptr_t tx_phys;
	uint32_t tx_prod;
	uint32_t rx_cons;
};

static struct bcmgenet_priv *g_genet;
static struct net_device *g_netdev;

static volatile uint32_t *genet_reg(uint32_t block, uint32_t off)
{
	return (volatile uint32_t *)((uintptr_t)g_genet->base + block + off);
}

static uint32_t genet_read(uint32_t block, uint32_t off)
{
	return *genet_reg(block, off);
}

static void genet_write(uint32_t block, uint32_t off, uint32_t value)
{
	*genet_reg(block, off) = value;
}

static void genet_write_desc(uint32_t dma_off, unsigned index, uint32_t status,
			     uintptr_t phys)
{
	volatile uint32_t *d =
	    (volatile uint32_t *)((uintptr_t)g_genet->base + dma_off +
				  index * DMA_DESC_SIZE);

	d[0] = status;
	d[1] = (uint32_t)phys;
	d[2] = (uint32_t)((uint64_t)phys >> 32);
}

static int mdio_wait(void)
{
	unsigned i;

	for (i = 0; i < 10000U; i++)
	{
		if ((genet_read(GENET_UMAC_OFF, UMAC_MDIO_CMD) &
		     MDIO_START_BUSY) == 0)
			return 0;
	}
		return -EIO;
}

static int mdio_read(uint32_t phy, uint32_t reg, uint16_t *value)
{
	int rc;

	genet_write(GENET_UMAC_OFF, UMAC_MDIO_CMD,
		    bcmgenet_mdio_read_cmd(phy, reg));
	rc = mdio_wait();
	if (rc < 0)
		return rc;
	if (genet_read(GENET_UMAC_OFF, UMAC_MDIO_CMD) & MDIO_READ_FAIL)
		return -EIO;
	*value = (uint16_t)(genet_read(GENET_UMAC_OFF, UMAC_MDIO_CMD) &
			    0xffffU);
	return 0;
}

static void genet_setup_ring(uint32_t dma_off, uintptr_t buf_pa)
{
	uint32_t ring_base;
	unsigned i;
	uint32_t status;

	for (i = 0; i < GENET_DESC_COUNT; i++)
	{
		status = bcmgenet_desc_status(GENET_MAX_FRAME,
					      DMA_OWN | DMA_SOP | DMA_EOP);
		if (i + 1U == GENET_DESC_COUNT)
			status |= DMA_WRAP;
		genet_write_desc(dma_off, i, status, buf_pa);
	}

	ring_base = dma_off + GENET_DEFAULT_RING * DMA_RING_SIZE;
	genet_write(0, ring_base + TDMA_CONS_INDEX, 0);
	genet_write(0, ring_base + TDMA_PROD_INDEX, 0);
	genet_write(0, ring_base + DMA_RING_BUF_SIZE,
		    bcmgenet_ring_buf_size(GENET_DESC_COUNT, GENET_MAX_FRAME));
	genet_write(0, ring_base + DMA_START_ADDR, 0);
	genet_write(0, ring_base + DMA_END_ADDR,
		    (GENET_DESC_COUNT * DMA_DESC_SIZE) / 4U - 1U);
	genet_write(0, ring_base + DMA_MBUF_DONE_THRESH, 1);

	/* DMA control block sits after the 17 ring register windows. */
	genet_write(dma_off, DMA_RINGS_SIZE + DMA_SCB_BURST_SIZE,
		    BCM2711_DMA_BURST);
	genet_write(dma_off, DMA_RINGS_SIZE + DMA_CTRL,
		    bcmgenet_dma_ctrl_enable(GENET_DEFAULT_RING));
}

static void genet_reset_umac(void)
{
	uint32_t cmd;

	genet_write(GENET_SYS_OFF, SYS_RBUF_FLUSH_CTRL, 1);
	genet_write(GENET_SYS_OFF, SYS_RBUF_FLUSH_CTRL, 0);
	genet_write(GENET_UMAC_OFF, UMAC_CMD, CMD_SW_RESET);
	cmd = genet_read(GENET_UMAC_OFF, UMAC_CMD);
	(void)cmd;
	genet_write(GENET_UMAC_OFF, UMAC_CMD, 0);
	genet_write(GENET_SYS_OFF, SYS_PORT_CTRL, PORT_MODE_EXT_GPHY);
	genet_write(GENET_UMAC_OFF, UMAC_MAC0, bcmgenet_umac_mac0(g_genet->mac));
	genet_write(GENET_UMAC_OFF, UMAC_MAC1, bcmgenet_umac_mac1(g_genet->mac));
	genet_write(GENET_UMAC_OFF, UMAC_MAX_FRAME_LEN, GENET_MAX_FRAME);
}

static void genet_rgmii_mode(void)
{
	uint32_t oob = RGMII_MODE_EN | RGMII_LINK;

	if (g_genet->rgmii_rxid)
		oob |= ID_MODE_DIS;
	genet_write(GENET_EXT_OFF, EXT_RGMII_OOB_CTRL, oob);
}

static int genet_phy_probe(void)
{
	uint16_t id1 = 0;
	uint16_t id2 = 0;
	int rc;

	rc = mdio_read(g_genet->phy_addr, 2, &id1);
	if (rc < 0)
		return rc;
	rc = mdio_read(g_genet->phy_addr, 3, &id2);
	if (rc < 0)
		return rc;
	if (id1 == 0xffffU && id2 == 0xffffU)
		return -ENODEV;
	g_genet->link_up = 1;
	return 0;
}

static int genet_send(struct net_device *dev, const void *data, size_t len)
{
	uint32_t ring_base;
	uint32_t status;
	uint32_t prod;

	if (!g_genet->base || !g_genet->tx_buf || !data || len == 0 ||
	    len > GENET_MAX_FRAME)
		return -EINVAL;
	if (len > GENET_MAX_MTU)
		return -EINVAL;
	memcpy(g_genet->tx_buf, data, len);
	dma_sync_for_device(g_genet->tx_buf, len);
	prod = g_genet->tx_prod % GENET_DESC_COUNT;
	status = bcmgenet_desc_status((uint32_t)len,
				      DMA_SOP | DMA_EOP | DMA_TX_APPEND_CRC |
					  DMA_OWN);
	if (prod + 1U == GENET_DESC_COUNT)
		status |= DMA_WRAP;
	genet_write_desc(GENET_TDMA_OFF, prod, status, g_genet->tx_phys);
	g_genet->tx_prod++;
	ring_base = GENET_TDMA_OFF + GENET_DEFAULT_RING * DMA_RING_SIZE;
	genet_write(0, ring_base + TDMA_PROD_INDEX,
		    g_genet->tx_prod & 0xffffU);
	dev->stats.tx_packets++;
	dev->stats.tx_bytes += len;
	return 0;
}

static void genet_poll_rx(void)
{
	uint32_t ring_base =
	    GENET_RDMA_OFF + GENET_DEFAULT_RING * DMA_RING_SIZE;
	uint32_t prod;
	uint32_t cons;

	if (!g_genet->base || !g_genet->rx_buf)
		return;
	prod = genet_read(0, ring_base + TDMA_PROD_INDEX) & 0xffffU;
	cons = g_genet->rx_cons;
	while (cons != prod)
	{
		unsigned idx = cons % GENET_DESC_COUNT;
		volatile uint32_t *d =
		    (volatile uint32_t *)((uintptr_t)g_genet->base +
					  GENET_RDMA_OFF +
					  idx * DMA_DESC_SIZE);
		uint32_t status = d[0];
		uint32_t len = (status >> DMA_BUFLENGTH_SHIFT) & 0x0fffU;

		if (status & DMA_OWN)
			break;
		dma_sync_for_cpu(g_genet->rx_buf, len);
		if (len >= 14U && len <= GENET_MAX_FRAME)
		{
			netif_rx(g_netdev, g_genet->rx_buf, len);
			g_netdev->stats.rx_packets++;
			g_netdev->stats.rx_bytes += len;
		}
		else
			g_netdev->stats.rx_errors++;
		genet_write_desc(GENET_RDMA_OFF, idx,
				 bcmgenet_desc_status(GENET_MAX_FRAME,
						      DMA_OWN | DMA_SOP |
							  DMA_EOP |
							  (idx + 1U ==
							   GENET_DESC_COUNT ?
							       DMA_WRAP :
							       0)),
				 g_genet->rx_phys);
		cons = (cons + 1U) & 0xffffU;
	}
	g_genet->rx_cons = cons;
	genet_write(0, ring_base + TDMA_CONS_INDEX, cons);
}

static void genet_poll(struct net_device *dev)
{
	(void)dev;
	genet_poll_rx();
}

static void genet_irq(uint32_t irq, void *data)
{
	(void)irq;
	(void)data;
	if (g_genet->base)
	{
		uint32_t stat = genet_read(GENET_INTRL2_0_OFF, INTRL2_CPU_STAT);

		genet_write(GENET_INTRL2_0_OFF, INTRL2_CPU_CLEAR, stat);
		if (stat & UMAC_IRQ_RXDMA_DONE)
			genet_poll_rx();
	}
}

static const struct net_device_ops genet_netdev_ops = {
	.ndo_start_xmit = genet_send,
	.ndo_set_mac_address = eth_mac_addr,
	.ndo_poll_controller = genet_poll,
};

static int genet_hw_init(struct net_device *dev)
{
	int rc;
	unsigned i;

	rc = dma_map_mmio(g_genet->mmio_pa, g_genet->mmio_size);
	if (rc < 0)
	{
		klog_info("GENET", "GENET_MMIO_MAP_FAIL");
		return rc;
	}
	g_genet->base = (volatile uint32_t *)g_genet->mmio_pa;
	g_genet->rx_buf = dma_alloc(GENET_MAX_FRAME, &g_genet->rx_phys);
	g_genet->tx_buf = dma_alloc(GENET_MAX_FRAME, &g_genet->tx_phys);
	if (!g_genet->rx_buf || !g_genet->tx_buf)
	{
		dma_free(g_genet->rx_buf, GENET_MAX_FRAME);
		dma_free(g_genet->tx_buf, GENET_MAX_FRAME);
		g_genet->rx_buf = NULL;
		g_genet->tx_buf = NULL;
		klog_info("GENET", "GENET_DMA_ALLOC_FAIL");
		return -ENOMEM;
	}

	genet_reset_umac();
	genet_rgmii_mode();
	genet_setup_ring(GENET_RDMA_OFF, g_genet->rx_phys);
	genet_setup_ring(GENET_TDMA_OFF, g_genet->tx_phys);
	rc = genet_phy_probe();
	if (rc < 0)
		klog_info("GENET", "GENET_PHY_ABSENT");
	else
		klog_info("GENET", "GENET_PHY_OK");

	genet_write(GENET_INTRL2_0_OFF, INTRL2_CPU_MASK_CLEAR,
		    UMAC_IRQ_RXDMA_DONE | UMAC_IRQ_TXDMA_DONE);
	for (i = 0; i < g_genet->irq_count && i < 2U; i++)
	{
		if (irq_request(g_genet->irq[i], genet_irq, g_genet,
				"bcmgenet") < 0)
			klog_info("GENET", "GENET_IRQ_SKIP");
		else if (i == 0)
			dev->irq = (int)g_genet->irq[0];
	}
	genet_write(GENET_UMAC_OFF, UMAC_CMD,
		    CMD_TX_EN | CMD_RX_EN | CMD_SPEED_1000);
	return 0;
}

int bcmgenet_init(void)
{
	const struct arm64_board_boot_info *info = arm64_board_boot_info();
	struct net_device *dev;
	unsigned i;

	if (g_netdev)
		return 0;
	if (!info || !info->genet_present || info->genet_mmio.base == 0 ||
	    info->genet_mmio.size == 0)
	{
		klog_info("GENET", "GENET_PROBE_ABSENT");
		return -ENODEV;
	}

	dev = alloc_etherdev(sizeof(struct bcmgenet_priv));
	if (!dev)
	{
		klog_info("GENET", "GENET_NETDEV_FAIL");
		return -ENOMEM;
	}
	g_genet = netdev_priv(dev);
	g_genet->mmio_pa = (uintptr_t)info->genet_mmio.base;
	g_genet->mmio_size = (size_t)info->genet_mmio.size;
	g_genet->irq_count = info->genet_irq_count;
	g_genet->irq[0] = info->genet_irq[0];
	g_genet->irq[1] = info->genet_irq[1];
	g_genet->phy_addr = info->genet_phy_addr ? info->genet_phy_addr : 1U;
	g_genet->rgmii_rxid = 0;
	if (info->genet_phy_mode[0] != '\0')
	{
		if (info->genet_phy_mode[0] == 'r' &&
		    info->genet_phy_mode[1] == 'g')
			g_genet->rgmii_rxid = 1;
	}
	else
		g_genet->rgmii_rxid = 1;
	for (i = 0; i < 6U; i++)
		g_genet->mac[i] = info->genet_mac[i];
	if (g_genet->mac[0] == 0 && g_genet->mac[1] == 0 && g_genet->mac[2] == 0 &&
	    g_genet->mac[3] == 0 && g_genet->mac[4] == 0 && g_genet->mac[5] == 0)
	{
		g_genet->mac[0] = 0xdc;
		g_genet->mac[1] = 0xa6;
		g_genet->mac[2] = 0x32;
		g_genet->mac[3] = 0x00;
		g_genet->mac[4] = 0x00;
		g_genet->mac[5] = 0x01;
	}

	if (genet_hw_init(dev) < 0)
	{
		klog_info("GENET", "GENET_PROBE_BLOCKED");
		g_genet = NULL;
		free_netdev(dev);
		return -ENXIO;
	}

	dev->name = "eth0";
	memcpy(dev->dev_addr, g_genet->mac, ETH_ALEN);
	dev->flags = IFF_BROADCAST | IFF_RUNNING;
	if (g_genet->link_up)
		dev->flags |= IFF_UP;
	dev->netdev_ops = &genet_netdev_ops;
	if (register_netdev(dev) != 0)
	{
		klog_info("GENET", "GENET_NETDEV_FAIL");
		return -EBUSY;
	}
	g_netdev = dev;
	if (g_genet->link_up)
		klog_info("GENET", "GENET_PROBE_OK");
	else
		klog_info("GENET", "GENET_PROBE_NOLINK");
	return 0;
}
