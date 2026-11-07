/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_bcmgenet.c
 * Description: Host contracts for GENET descriptor, MDIO, and burst encoding.
 */

#include "test_harness.h"

#include <drivers/net/bcmgenet.h>

void test_bcmgenet_register_contract(void)
{
	uint8_t mac[6] = { 0xdc, 0xa6, 0x32, 0x00, 0x00, 0x01 };
	uint32_t status;
	uint32_t cmd;

	TEST_BEGIN("bcmgenet_desc_and_mac");
	status = bcmgenet_desc_status(64, DMA_SOP | DMA_EOP | DMA_OWN);
	ASSERT_EQ((status >> DMA_BUFLENGTH_SHIFT) & 0x0fffU, 64U);
	ASSERT(status & DMA_OWN);
	ASSERT(status & DMA_SOP);
	ASSERT_EQ(bcmgenet_umac_mac0(mac), 0xdca63200U);
	ASSERT_EQ(bcmgenet_umac_mac1(mac), 0x0001U);
	TEST_END();

	TEST_BEGIN("bcmgenet_mdio_and_burst");
	cmd = bcmgenet_mdio_read_cmd(1, 2);
	ASSERT(cmd & MDIO_START_BUSY);
	ASSERT(cmd & MDIO_RD);
	ASSERT_EQ((cmd >> MDIO_PMD_SHIFT) & 0x1fU, 1U);
	ASSERT_EQ((cmd >> MDIO_REG_SHIFT) & 0x1fU, 2U);
	ASSERT_EQ(BCM2711_DMA_BURST, 0x08U);
	ASSERT(bcmgenet_dma_ctrl_enable(GENET_DEFAULT_RING) & DMA_EN);
	ASSERT_EQ(bcmgenet_ring_buf_size(256, 1536) >> 16, 256U);
	TEST_END();
}
