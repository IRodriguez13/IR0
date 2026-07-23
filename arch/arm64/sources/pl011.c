/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pl011.c
 * Description: PL011 UART — base from arm64_board (0 = no-op, honest stub).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "pl011.h"

#include <ir0/arm64_board.h>

#include <stdint.h>

#define PL011_FR_TXFF	  (1u << 5)
#define PL011_CR_UARTEN	  (1u << 0)
#define PL011_CR_TXE	  (1u << 8)
#define PL011_CR_RXE	  (1u << 9)
#define PL011_LCRH_WLEN_8 (3u << 5)
#define PL011_LCRH_FEN	  (1u << 4)

static uintptr_t g_pl011_base;

static volatile uint32_t *pl011_reg(uintptr_t off)
{
	return (volatile uint32_t *)(g_pl011_base + off);
}

void pl011_init_at(uintptr_t base)
{
	g_pl011_base = base;
	if (!g_pl011_base)
	{
		return;
	}

	/* Disable while programming; QEMU pre-inits UART but sequence is safe. */
	*pl011_reg(0x30UL) = 0;
	*pl011_reg(0x24UL) = 1;
	*pl011_reg(0x28UL) = 0;
	*pl011_reg(0x2CUL) = PL011_LCRH_WLEN_8 | PL011_LCRH_FEN;
	*pl011_reg(0x30UL) = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

void pl011_init(void)
{
	const struct arm64_board_desc *b = arm64_board_get();

	pl011_init_at(b ? b->uart_mmio : 0x09000000UL);
}

void pl011_putc(char c)
{
	if (!g_pl011_base)
	{
		return;
	}

	while (*pl011_reg(0x18UL) & PL011_FR_TXFF)
	{
		;
	}
	*pl011_reg(0x00UL) = (uint32_t)(uint8_t)c;
}

void pl011_puts(const char *s)
{
	while (s && *s)
	{
		if (*s == '\n')
		{
			pl011_putc('\r');
		}
		pl011_putc(*s++);
	}
}
