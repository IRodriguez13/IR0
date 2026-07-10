/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci.c
 * Description: Minimal AHCI PCI class detect (no full command engine yet).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/serial_io.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_CLASS_MASS     0x01
#define PCI_SUBCLASS_AHCI  0x06

static inline void outl(uint16_t port, uint32_t val)
{
	__asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port)
{
	uint32_t val;

	__asm__ __volatile__("inl %1, %0" : "=a"(val) : "Nd"(port));
	return val;
}

static uint32_t ahci_pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t offset)
{
	uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			   ((uint32_t)func << 8) | (offset & 0xfc) | 0x80000000U;

	outl(PCI_CONFIG_ADDRESS, address);
	return inl(PCI_CONFIG_DATA);
}

void ahci_probe(void)
{
	uint8_t bus, slot, func;

	for (bus = 0; bus < 8; bus++)
	{
		for (slot = 0; slot < 32; slot++)
		{
			for (func = 0; func < 8; func++)
			{
				uint32_t id = ahci_pci_read(bus, slot, func, 0);
				uint32_t class_rev;
				uint8_t pci_class, pci_subclass;

				if ((id & 0xFFFF) == 0xFFFF)
					continue;
				class_rev = ahci_pci_read(bus, slot, func, 0x08);
				pci_class = (uint8_t)((class_rev >> 24) & 0xFFU);
				pci_subclass = (uint8_t)((class_rev >> 16) & 0xFFU);
				if (pci_class == PCI_CLASS_MASS &&
				    pci_subclass == PCI_SUBCLASS_AHCI)
				{
					serial_print("AHCI_DETECT_OK\n");
					return;
				}
			}
		}
	}
	serial_print("AHCI_DETECT_NONE\n");
}
