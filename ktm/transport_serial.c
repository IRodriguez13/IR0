/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: transport_serial.c
 * Description: KTM serial protocol — KTM|seq|KIND|name|status
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ktm_internal.h"
#include <ir0/serial_io.h>
#include <stdint.h>

static uint64_t g_tx_seq;

static void ktm_print_u64_dec(uint64_t v)
{
	char tmp[24];
	int i = 0;

	if (v == 0)
	{
		serial_print("0");
		return;
	}
	while (v > 0 && i < (int)sizeof(tmp) - 1)
	{
		tmp[i++] = (char)('0' + (v % 10));
		v /= 10;
	}
	while (i > 0)
	{
		char one[2];

		one[0] = tmp[--i];
		one[1] = '\0';
		serial_print(one);
	}
}

void ktm_transport_emit(const char *kind, const char *name, const char *status)
{
	g_tx_seq++;
	serial_print("KTM|");
	ktm_print_u64_dec(g_tx_seq);
	serial_print("|");
	serial_print(kind ? kind : "?");
	serial_print("|");
	serial_print(name ? name : "?");
	if (status && status[0])
	{
		serial_print("|");
		serial_print(status);
	}
	serial_print("\n");
}

void ktm_transport_emit_u64(const char *kind, const char *name, uint64_t value)
{
	char hex[20];
	int i;

	g_tx_seq++;
	serial_print("KTM|");
	ktm_print_u64_dec(g_tx_seq);
	serial_print("|");
	serial_print(kind ? kind : "?");
	serial_print("|");
	serial_print(name ? name : "?");
	serial_print("|0x");
	for (i = 15; i >= 0; i--)
	{
		unsigned nibble = (unsigned)((value >> (i * 4)) & 0xF);
		hex[15 - i] = (char)(nibble < 10 ? '0' + nibble : 'a' + nibble - 10);
	}
	hex[16] = '\0';
	serial_print(hex);
	serial_print("\n");
}

void ktm_transport_suite_end(unsigned pass, unsigned fail)
{
	g_tx_seq++;
	serial_print("KTM|");
	ktm_print_u64_dec(g_tx_seq);
	serial_print("|SUITE_END|pass=");
	ktm_print_u64_dec(pass);
	serial_print("|fail=");
	ktm_print_u64_dec(fail);
	serial_print("\n");
	/* Autokill-friendly single tag. */
	if (fail == 0)
		serial_print("KTM_SUITE_OK\n");
	else
		serial_print("KTM_SUITE_FAIL\n");
}
