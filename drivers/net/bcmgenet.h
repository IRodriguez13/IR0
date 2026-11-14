/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: bcmgenet.h
 * Description: BCM2711 GENET v5 register map and descriptor helpers.
 *
 * References:
 *   https://www.kernel.org/doc/Documentation/devicetree/bindings/net/brcm,bcmgenet.yaml
 *   Linux drivers/net/ethernet/broadcom/genet/bcmgenet.h
 */

#pragma once

#include <stdint.h>

#define GENET_SYS_OFF		0x0000U
#define GENET_EXT_OFF		0x0080U
#define GENET_INTRL2_0_OFF	0x0200U
#define GENET_INTRL2_1_OFF	0x0240U
#define GENET_RBUF_OFF		0x0300U
#define GENET_UMAC_OFF		0x0800U
#define GENET_RDMA_OFF		0x2000U
#define GENET_TDMA_OFF		0x4000U

#define SYS_REV_CTRL		0x00U
#define SYS_PORT_CTRL		0x04U
#define PORT_MODE_EXT_GPHY	3U
#define SYS_RBUF_FLUSH_CTRL	0x08U
#define SYS_TBUF_FLUSH_CTRL	0x0CU

#define EXT_RGMII_OOB_CTRL	0x0CU
#define RGMII_LINK		(1U << 4)
#define OOB_DISABLE		(1U << 5)
#define RGMII_MODE_EN		(1U << 6)
#define ID_MODE_DIS		(1U << 16)

#define INTRL2_CPU_STAT		0x00U
#define INTRL2_CPU_CLEAR	0x08U
#define INTRL2_CPU_MASK_SET	0x10U
#define INTRL2_CPU_MASK_CLEAR	0x14U

#define UMAC_CMD		0x008U
#define CMD_TX_EN		0x01U
#define CMD_RX_EN		0x02U
#define CMD_SPEED_1000		0x08U
#define CMD_PROMISC		0x10U
#define CMD_SW_RESET		0x2000U
#define UMAC_MAC0		0x00CU
#define UMAC_MAC1		0x010U
#define UMAC_MAX_FRAME_LEN	0x014U
#define UMAC_MDIO_CMD		0x614U

#define MDIO_START_BUSY		(1U << 29)
#define MDIO_READ_FAIL		(1U << 28)
#define MDIO_RD			(2U << 26)
#define MDIO_WR			(1U << 26)
#define MDIO_PMD_SHIFT		21U
#define MDIO_REG_SHIFT		16U

#define DMA_DESC_LENGTH_STATUS	0x00U
#define DMA_DESC_ADDRESS_LO	0x04U
#define DMA_DESC_ADDRESS_HI	0x08U
#define DMA_DESC_SIZE		12U
#define GENET_DESC_COUNT	256U
#define GENET_DEFAULT_RING	16U
#define DMA_RING_SIZE		0x40U
#define DMA_RINGS_SIZE		(DMA_RING_SIZE * (GENET_DEFAULT_RING + 1U))

#define DMA_CTRL		0x04U
#define DMA_STATUS		0x08U
#define DMA_SCB_BURST_SIZE	0x0CU
#define DMA_EN			(1U << 0)
#define DMA_RING_BUF_EN_SHIFT	1U
#define BCM2711_DMA_BURST	0x08U

#define TDMA_PROD_INDEX		0x0CU
#define TDMA_CONS_INDEX		0x08U
#define DMA_RING_BUF_SIZE	0x10U
#define DMA_START_ADDR		0x14U
#define DMA_END_ADDR		0x1CU
#define DMA_MBUF_DONE_THRESH	0x24U

#define DMA_BUFLENGTH_SHIFT	16U
#define DMA_OWN			0x8000U
#define DMA_EOP			0x4000U
#define DMA_SOP			0x2000U
#define DMA_WRAP		0x1000U
#define DMA_TX_APPEND_CRC	0x0040U

#define UMAC_IRQ_RXDMA_DONE	(1U << 13)
#define UMAC_IRQ_TXDMA_DONE	(1U << 16)

#define GENET_MAX_MTU		1536U
#define GENET_MAX_FRAME		1536U

static inline uint32_t bcmgenet_desc_status(uint32_t len, uint32_t flags)
{
	return ((len & 0x0fffU) << DMA_BUFLENGTH_SHIFT) | flags;
}

static inline uint32_t bcmgenet_mdio_read_cmd(uint32_t phy, uint32_t reg)
{
	return MDIO_START_BUSY | MDIO_RD |
	       ((phy & 0x1fU) << MDIO_PMD_SHIFT) |
	       ((reg & 0x1fU) << MDIO_REG_SHIFT);
}

static inline uint32_t bcmgenet_dma_ctrl_enable(unsigned ring)
{
	return DMA_EN | (1U << (DMA_RING_BUF_EN_SHIFT + ring));
}

static inline uint32_t bcmgenet_umac_mac0(const uint8_t mac[6])
{
	return ((uint32_t)mac[0] << 24) | ((uint32_t)mac[1] << 16) |
	       ((uint32_t)mac[2] << 8) | (uint32_t)mac[3];
}

static inline uint32_t bcmgenet_umac_mac1(const uint8_t mac[6])
{
	return ((uint32_t)mac[4] << 8) | (uint32_t)mac[5];
}

static inline uint32_t bcmgenet_ring_buf_size(uint32_t desc_count,
					      uint32_t buf_len)
{
	return (desc_count << 16) | (buf_len & 0xffffU);
}

int bcmgenet_init(void);
