// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: serial.c
 * Description: Serial port driver for debugging output
 */

#include <stdint.h>
#include <stdbool.h>
#include <arch/common/arch_interface.h>

/* Compiler optimization hints */
#define likely(x)	__builtin_expect(!!(x), 1)
#define unlikely(x)	__builtin_expect(!!(x), 0)

/* Serial port definitions */
#define SERIAL_PORT_COM1	0x3F8
#define SERIAL_DATA_REG		0
#define SERIAL_INT_EN_REG	1
#define SERIAL_FIFO_CTRL_REG	2
#define SERIAL_LINE_CTRL_REG	3
#define SERIAL_MODEM_CTRL_REG	4
#define SERIAL_LINE_STATUS_REG	5

/* Line status register bits */
#define SERIAL_LSR_THRE		0x20	/* Transmitter holding register empty */

static bool serial_initialized = false;

/**
 * serial_init - initialize serial port for debugging
 *
 * Configure COM1 port for 38400 baud, 8N1.
 */
void serial_init(void)
{
	if (serial_initialized)
		return;

	/* Disable interrupts */
	outb(SERIAL_PORT_COM1 + SERIAL_INT_EN_REG, 0x00);

	/* Set baud rate divisor (38400 baud) */
	outb(SERIAL_PORT_COM1 + SERIAL_LINE_CTRL_REG, 0x80);	/* Enable DLAB */
	outb(SERIAL_PORT_COM1 + SERIAL_DATA_REG, 0x03);	/* Divisor low byte */
	outb(SERIAL_PORT_COM1 + SERIAL_INT_EN_REG, 0x00);	/* Divisor high byte */

	/* Configure: 8 bits, no parity, one stop bit */
	outb(SERIAL_PORT_COM1 + SERIAL_LINE_CTRL_REG, 0x03);

	/* Enable FIFO, clear them, with 14-byte threshold */
	outb(SERIAL_PORT_COM1 + SERIAL_FIFO_CTRL_REG, 0xC7);

	/* Enable IRQs, set RTS/DSR */
	outb(SERIAL_PORT_COM1 + SERIAL_MODEM_CTRL_REG, 0x0B);

	serial_initialized = true;
}

/**
 * serial_is_transmit_empty - check if transmitter is ready
 *
 * Returns true if transmitter holding register is empty.
 */
static bool serial_is_transmit_empty(void)
{
	return inb(SERIAL_PORT_COM1 + SERIAL_LINE_STATUS_REG) & SERIAL_LSR_THRE;
}

/**
 * serial_putchar - send a character to serial port
 * @c: character to send
 */
void serial_putchar(char c)
{
	if (unlikely(!serial_initialized))
		serial_init();

	while (!serial_is_transmit_empty())
		;
	
	outb(SERIAL_PORT_COM1, c);
}

void serial_print(const char *str)
{
    if (!str) return;
    
    while (*str) {
        serial_putchar(*str);
        str++;
    }
}

void serial_print_hex32(uint32_t num)
{
    serial_print("0x");
    for (int i = 7; i >= 0; i--) {
        int digit = (num >> (i * 4)) & 0xF;
        char c = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        serial_putchar(c);
    }
}
void serial_print_hex64(uint64_t num)
{
    serial_print("0x");
    for (int i = 15; i >= 0; i--) {
        int digit = (num >> (i * 4)) & 0xF;
        char c = (digit < 10) ? '0' + digit : 'a' + digit - 10;
        serial_putchar(c);
    }
}
