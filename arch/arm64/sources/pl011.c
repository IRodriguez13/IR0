/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: pl011.c
 * Description: PL011 UART0 for QEMU virt — shared freestanding console backend.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "pl011.h"

#include <stdint.h>

#define PL011_BASE   0x09000000UL
#define PL011_DR     (*(volatile uint32_t *)(PL011_BASE + 0x00UL))
#define PL011_FR     (*(volatile uint32_t *)(PL011_BASE + 0x18UL))
#define PL011_IBRD   (*(volatile uint32_t *)(PL011_BASE + 0x24UL))
#define PL011_FBRD   (*(volatile uint32_t *)(PL011_BASE + 0x28UL))
#define PL011_LCRH   (*(volatile uint32_t *)(PL011_BASE + 0x2CUL))
#define PL011_CR     (*(volatile uint32_t *)(PL011_BASE + 0x30UL))

#define PL011_FR_TXFF (1u << 5)
#define PL011_CR_UARTEN (1u << 0)
#define PL011_CR_TXE    (1u << 8)
#define PL011_CR_RXE    (1u << 9)
#define PL011_LCRH_WLEN_8 (3u << 5)
#define PL011_LCRH_FEN    (1u << 4)

void pl011_init(void)
{
	/* Disable while programming; QEMU pre-inits UART but sequence is safe. */
	PL011_CR = 0;
	PL011_IBRD = 1;
	PL011_FBRD = 0;
	PL011_LCRH = PL011_LCRH_WLEN_8 | PL011_LCRH_FEN;
	PL011_CR = PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE;
}

void pl011_putc(char c)
{
	while (PL011_FR & PL011_FR_TXFF)
	{
		;
	}
	PL011_DR = (uint32_t)(uint8_t)c;
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
