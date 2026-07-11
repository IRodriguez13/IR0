/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: ahci.c
 * Description: AHCI DMA/NCQ read/write (up to 2 ports) + ir0_block_* register.
 *
 * Reference: https://wiki.osdev.org/AHCI (HBA port init, DMA EXT, FPDMA NCQ).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "ahci.h"

#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/serial_io.h>
#include <mm/paging.h>
#include <string.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_CLASS_MASS     0x01
#define PCI_SUBCLASS_AHCI  0x06

#define AHCI_GHC_AE        (1u << 31)
#define AHCI_GHC_IE        (1u << 1)
#define AHCI_GHC_HR        (1u << 0)

#define AHCI_PORT_CMD_ST   (1u << 0)
#define AHCI_PORT_CMD_FRE  (1u << 4)
#define AHCI_PORT_CMD_FR   (1u << 14)
#define AHCI_PORT_CMD_CR   (1u << 15)

#define AHCI_SSTS_DET_MASK 0x0Fu
#define AHCI_SSTS_DET_PRES 0x03u

#define ATA_DEV_BUSY       0x80
#define ATA_DEV_DRQ        0x08
#define ATA_CMD_READ_DMA_EX  0x25
#define ATA_CMD_WRITE_DMA_EX 0x35
#define ATA_CMD_READ_FPDMA   0x60
#define ATA_CMD_WRITE_FPDMA  0x61
#define ATA_CMD_IDENTIFY     0xEC

#define FIS_TYPE_REG_H2D   0x27

#define AHCI_SECTOR_SIZE   512u
#define AHCI_ABAR_MAP_PAGES 8u
#define AHCI_CAP_SNCQ      (1u << 30)
#define AHCI_CAP_NCS_MASK  0x1F00u
#define AHCI_CAP_NCS_SHIFT 8
#define AHCI_CMD_SLOTS     8u
#define AHCI_CLB_BYTES     1024u
#define AHCI_CTBA_BYTES    256u

struct hba_port
{
	uint32_t clb;
	uint32_t clbu;
	uint32_t fb;
	uint32_t fbu;
	uint32_t is;
	uint32_t ie;
	uint32_t cmd;
	uint32_t rsv0;
	uint32_t tfd;
	uint32_t sig;
	uint32_t ssts;
	uint32_t sctl;
	uint32_t serr;
	uint32_t sact;
	uint32_t ci;
	uint32_t sntf;
	uint32_t fbs;
	uint32_t rsv1[11];
	uint32_t vendor[4];
};

struct hba_mem
{
	uint32_t cap;
	uint32_t ghc;
	uint32_t is;
	uint32_t pi;
	uint32_t vs;
	uint32_t ccc_ctl;
	uint32_t ccc_pts;
	uint32_t em_loc;
	uint32_t em_ctl;
	uint32_t cap2;
	uint32_t bohc;
	uint8_t  rsv[0xA0 - 0x2C];
	uint8_t  vendor[0x100 - 0xA0];
	struct hba_port ports[32];
};

struct hba_cmd_header
{
	uint8_t  cfl : 5;
	uint8_t  a : 1;
	uint8_t  w : 1;
	uint8_t  p : 1;
	uint8_t  r : 1;
	uint8_t  b : 1;
	uint8_t  c : 1;
	uint8_t  rsv0 : 1;
	uint8_t  pmp : 4;
	uint16_t prdtl;
	volatile uint32_t prdbc;
	uint32_t ctba;
	uint32_t ctbau;
	uint32_t rsv1[4];
};

struct hba_prdt_entry
{
	uint32_t dba;
	uint32_t dbau;
	uint32_t rsv0;
	uint32_t dbc : 22;
	uint32_t rsv1 : 9;
	uint32_t i : 1;
};

struct hba_cmd_tbl
{
	uint8_t  cfis[64];
	uint8_t  acmd[16];
	uint8_t  rsv[48];
	struct hba_prdt_entry prdt_entry[1];
};

struct fis_reg_h2d
{
	uint8_t  fis_type;
	uint8_t  pmport : 4;
	uint8_t  rsv0 : 3;
	uint8_t  c : 1;
	uint8_t  command;
	uint8_t  featurel;
	uint8_t  lba0;
	uint8_t  lba1;
	uint8_t  lba2;
	uint8_t  device;
	uint8_t  lba3;
	uint8_t  lba4;
	uint8_t  lba5;
	uint8_t  featureh;
	uint8_t  countl;
	uint8_t  counth;
	uint8_t  icc;
	uint8_t  control;
	uint8_t  rsv1[4];
};

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

static uint32_t ahci_pci_read(uint8_t bus, uint8_t slot, uint8_t func,
			      uint8_t offset)
{
	uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			   ((uint32_t)func << 8) | (offset & 0xfc) | 0x80000000U;

	outl(PCI_CONFIG_ADDRESS, address);
	return inl(PCI_CONFIG_DATA);
}

static void ahci_pci_write(uint8_t bus, uint8_t slot, uint8_t func,
			   uint8_t offset, uint32_t value)
{
	uint32_t address = ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			   ((uint32_t)func << 8) | (offset & 0xfc) | 0x80000000U;

	outl(PCI_CONFIG_ADDRESS, address);
	outl(PCI_CONFIG_DATA, value);
}

/* DMA structures: BSS is identity-mapped in low kernel memory. */
#define AHCI_MAX_PORTS 2

struct ahci_slot
{
	volatile struct hba_port *port;
	int ready;
	int registered;
	int ncq_ok;
	uint64_t nsectors;
	char name[8];
	uint8_t clb[AHCI_CLB_BYTES] __attribute__((aligned(1024)));
	uint8_t fb[256] __attribute__((aligned(256)));
	uint8_t ctba[AHCI_CMD_SLOTS][AHCI_CTBA_BYTES]
		__attribute__((aligned(128)));
	uint8_t io_buf[AHCI_SECTOR_SIZE] __attribute__((aligned(16)));
	uint8_t ident[AHCI_SECTOR_SIZE] __attribute__((aligned(16)));
};

static struct ahci_slot ahci_slots[AHCI_MAX_PORTS];
static int ahci_nslots;
static int ahci_hba_sncq;
static int ahci_hba_ncs;

static volatile struct hba_mem *ahci_abar;
static uintptr_t ahci_abar_phys;

static void ahci_delay(void)
{
	volatile int i;

	for (i = 0; i < 10000; i++)
		;
}

static void ahci_stop_cmd(volatile struct hba_port *port)
{
	uint32_t spin;

	port->cmd &= ~(AHCI_PORT_CMD_ST | AHCI_PORT_CMD_FRE);
	for (spin = 0; spin < 500000; spin++)
	{
		if (!(port->cmd & (AHCI_PORT_CMD_FR | AHCI_PORT_CMD_CR)))
			break;
		ahci_delay();
	}
}

static void ahci_start_cmd(volatile struct hba_port *port)
{
	uint32_t spin;

	for (spin = 0; spin < 500000; spin++)
	{
		if (!(port->cmd & AHCI_PORT_CMD_CR))
			break;
		ahci_delay();
	}
	port->cmd |= AHCI_PORT_CMD_FRE;
	port->cmd |= AHCI_PORT_CMD_ST;
}

static int ahci_port_rebase(struct ahci_slot *slot)
{
	struct hba_cmd_header *hdr;
	volatile struct hba_port *port;
	unsigned i;
	unsigned nslots;

	if (!slot || !slot->port)
		return -EINVAL;
	port = slot->port;
	ahci_stop_cmd(port);

	port->clb = (uint32_t)(uintptr_t)slot->clb;
	port->clbu = 0;
	port->fb = (uint32_t)(uintptr_t)slot->fb;
	port->fbu = 0;
	memset(slot->clb, 0, sizeof(slot->clb));
	memset(slot->fb, 0, sizeof(slot->fb));
	memset(slot->ctba, 0, sizeof(slot->ctba));

	nslots = (unsigned)ahci_hba_ncs;
	if (nslots == 0 || nslots > AHCI_CMD_SLOTS)
		nslots = AHCI_CMD_SLOTS;

	hdr = (struct hba_cmd_header *)slot->clb;
	for (i = 0; i < nslots; i++)
	{
		hdr[i].ctba = (uint32_t)(uintptr_t)slot->ctba[i];
		hdr[i].ctbau = 0;
		hdr[i].prdtl = 1;
	}

	ahci_start_cmd(port);
	return 0;
}

static int ahci_find_cmdslot(volatile struct hba_port *port)
{
	uint32_t slots;
	unsigned i;
	unsigned nslots;

	if (!port)
		return -1;
	nslots = (unsigned)ahci_hba_ncs;
	if (nslots == 0 || nslots > AHCI_CMD_SLOTS)
		nslots = 1;
	slots = port->sact | port->ci;
	for (i = 0; i < nslots; i++)
	{
		if ((slots & (1u << i)) == 0)
			return (int)i;
	}
	return -1;
}

static int ahci_issue_rw(struct ahci_slot *slot, uint64_t lba, uint32_t count,
			 void *buf, int write)
{
	volatile struct hba_port *port;
	struct hba_cmd_header *hdr;
	struct hba_cmd_tbl *tbl;
	struct fis_reg_h2d *fis;
	uint32_t spin;
	uintptr_t phys;
	int cmdslot;
	int use_ncq;
	uint32_t bit;

	if (!slot || !slot->ready || !slot->port || !buf || count == 0 ||
	    count > 1)
		return -EINVAL;

	port = slot->port;
	phys = (uintptr_t)buf;
	port->is = (uint32_t)-1;

	spin = 0;
	while ((port->tfd & (ATA_DEV_BUSY | ATA_DEV_DRQ)) && spin < 1000000)
	{
		spin++;
		ahci_delay();
	}
	if (spin >= 1000000)
		return -EIO;

	cmdslot = ahci_find_cmdslot(port);
	if (cmdslot < 0)
		return -EBUSY;
	bit = 1u << (unsigned)cmdslot;

	use_ncq = ahci_hba_sncq && slot->ncq_ok;

	hdr = &((struct hba_cmd_header *)slot->clb)[cmdslot];
	hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
	hdr->w = write ? 1 : 0;
	hdr->prdtl = 1;
	hdr->prdbc = 0;

	tbl = (struct hba_cmd_tbl *)slot->ctba[cmdslot];
	memset(tbl, 0, sizeof(*tbl));
	tbl->prdt_entry[0].dba = (uint32_t)phys;
	tbl->prdt_entry[0].dbau = (uint32_t)(phys >> 32);
	tbl->prdt_entry[0].dbc = (count * AHCI_SECTOR_SIZE) - 1;
	tbl->prdt_entry[0].i = 1;

	fis = (struct fis_reg_h2d *)tbl->cfis;
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->c = 1;
	fis->device = 1u << 6;
	fis->lba0 = (uint8_t)(lba);
	fis->lba1 = (uint8_t)(lba >> 8);
	fis->lba2 = (uint8_t)(lba >> 16);
	fis->lba3 = (uint8_t)(lba >> 24);
	fis->lba4 = (uint8_t)(lba >> 32);
	fis->lba5 = (uint8_t)(lba >> 40);

	if (use_ncq)
	{
		fis->command = write ? ATA_CMD_WRITE_FPDMA : ATA_CMD_READ_FPDMA;
		fis->featurel = (uint8_t)(count & 0xFF);
		fis->featureh = (uint8_t)((count >> 8) & 0xFF);
		/* NCQ tag in Sector Count[7:3]. */
		fis->countl = (uint8_t)(((unsigned)cmdslot & 0x1Fu) << 3);
		fis->counth = 0;
		port->sact |= bit;
		port->ci |= bit;
	}
	else
	{
		fis->command = write ? ATA_CMD_WRITE_DMA_EX : ATA_CMD_READ_DMA_EX;
		fis->featurel = 0;
		fis->featureh = 0;
		fis->countl = (uint8_t)(count & 0xFF);
		fis->counth = (uint8_t)((count >> 8) & 0xFF);
		port->ci = bit;
	}

	spin = 0;
	while (((port->ci | port->sact) & bit) && spin < 2000000)
	{
		if (port->is & (1u << 30))
			return -EIO;
		spin++;
		ahci_delay();
	}
	if ((port->ci | port->sact) & bit)
		return -EIO;
	return 0;
}

static int ahci_identify(struct ahci_slot *slot)
{
	volatile struct hba_port *port;
	struct hba_cmd_header *hdr;
	struct hba_cmd_tbl *tbl;
	struct fis_reg_h2d *fis;
	uint32_t spin;
	uint16_t *id;
	uint64_t sectors;

	if (!slot || !slot->port)
		return -ENODEV;
	port = slot->port;

	port->is = (uint32_t)-1;
	hdr = &((struct hba_cmd_header *)slot->clb)[0];
	hdr->cfl = sizeof(struct fis_reg_h2d) / 4;
	hdr->w = 0;
	hdr->prdtl = 1;
	hdr->prdbc = 0;

	tbl = (struct hba_cmd_tbl *)slot->ctba[0];
	memset(tbl, 0, sizeof(*tbl));
	tbl->prdt_entry[0].dba = (uint32_t)(uintptr_t)slot->ident;
	tbl->prdt_entry[0].dbau = 0;
	tbl->prdt_entry[0].dbc = AHCI_SECTOR_SIZE - 1;
	tbl->prdt_entry[0].i = 1;

	fis = (struct fis_reg_h2d *)tbl->cfis;
	fis->fis_type = FIS_TYPE_REG_H2D;
	fis->c = 1;
	fis->command = ATA_CMD_IDENTIFY;
	fis->device = 0;

	port->ci = 1;
	spin = 0;
	while ((port->ci & 1) && spin < 2000000)
	{
		if (port->is & (1u << 30))
			return -EIO;
		spin++;
		ahci_delay();
	}
	if (port->ci & 1)
		return -EIO;

	id = (uint16_t *)slot->ident;
	sectors = ((uint64_t)id[61] << 16) | id[60];
	if (sectors == 0)
		sectors = ((uint64_t)id[103] << 48) | ((uint64_t)id[102] << 32) |
			  ((uint64_t)id[101] << 16) | id[100];
	slot->nsectors = sectors ? sectors : 1;
	/* Word 76 bit 8: NCQ support (ATA ACS / SATA). */
	slot->ncq_ok = (id[76] & (1u << 8)) ? 1 : 0;
	return 0;
}

static int ahci_backend_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	struct ahci_slot *slot = ctx;
	uint8_t *dst = buf;
	uint32_t i;
	int ret;

	if (!slot || !slot->ready || !buf || count == 0)
		return -EINVAL;
	for (i = 0; i < count; i++)
	{
		ret = ahci_issue_rw(slot, lba + i, 1, slot->io_buf, 0);
		if (ret < 0)
			return ret;
		memcpy(dst + (size_t)i * AHCI_SECTOR_SIZE, slot->io_buf,
		       AHCI_SECTOR_SIZE);
	}
	return 0;
}

static int ahci_backend_write(void *ctx, uint64_t lba, uint32_t count,
			      const void *buf)
{
	struct ahci_slot *slot = ctx;
	const uint8_t *src = buf;
	uint32_t i;
	int ret;

	if (!slot || !slot->ready || !buf || count == 0)
		return -EINVAL;
	for (i = 0; i < count; i++)
	{
		memcpy(slot->io_buf, src + (size_t)i * AHCI_SECTOR_SIZE,
		       AHCI_SECTOR_SIZE);
		ret = ahci_issue_rw(slot, lba + i, 1, slot->io_buf, 1);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct ir0_block_ops ahci_block_ops = {
	.read = ahci_backend_read,
	.write = ahci_backend_write,
	.flush = NULL,
};

static void ahci_register_block(struct ahci_slot *slot)
{
	struct ir0_block_device dev;

	if (!slot || slot->registered || !slot->ready)
		return;

	memset(&dev, 0, sizeof(dev));
	dev.ctx = slot;
	dev.ops = &ahci_block_ops;
	dev.info.sector_size = AHCI_SECTOR_SIZE;
	dev.info.max_sectors_per_io = 1;
	dev.info.sector_count = slot->nsectors;
	dev.info.flags = IR0_BLOCK_FLAG_DMA_CAPABLE;
	memcpy(dev.info.name, slot->name, sizeof(slot->name));

	if (ir0_block_register(&dev) == 0)
	{
		slot->registered = 1;
		serial_print("[AHCI] registered block ");
		serial_print(slot->name);
		serial_print("\n");
	}
}

void ahci_map_mmio_in_directory(uint64_t *pml4)
{
	uint32_t off;

	if (!pml4 || ahci_abar_phys == 0)
		return;

	for (off = 0; off < AHCI_ABAR_MAP_PAGES * PAGE_SIZE_4KB;
	     off += PAGE_SIZE_4KB)
	{
		uint64_t p = (uint64_t)ahci_abar_phys + off;

		(void)map_page_in_directory(pml4, p, p,
					    PAGE_PRESENT | PAGE_RW |
						    PAGE_CACHE_DISABLE);
	}
}

int ahci_disk_present(void)
{
	return ahci_nslots > 0 && ahci_slots[0].ready;
}

uint64_t ahci_sector_count(void)
{
	return ahci_nslots > 0 ? ahci_slots[0].nsectors : 0;
}

void ahci_probe(void)
{
	uint8_t bus, slot, func;
	uint32_t bar5, cmd;
	uint32_t pi;
	uint32_t cap;
	int port_idx;
	static const char *names[AHCI_MAX_PORTS] = { "sda", "sdb" };

	ahci_nslots = 0;
	ahci_hba_sncq = 0;
	ahci_hba_ncs = 1;
	memset(ahci_slots, 0, sizeof(ahci_slots));

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
				pci_subclass =
					(uint8_t)((class_rev >> 16) & 0xFFU);
				if (pci_class != PCI_CLASS_MASS ||
				    pci_subclass != PCI_SUBCLASS_AHCI)
					continue;

				serial_print("AHCI_DETECT_OK\n");

				cmd = ahci_pci_read(bus, slot, func, 0x04);
				cmd |= 0x6;
				ahci_pci_write(bus, slot, func, 0x04, cmd);

				bar5 = ahci_pci_read(bus, slot, func, 0x24);
				ahci_abar_phys = (uintptr_t)(bar5 & ~0xFu);
				if (ahci_abar_phys == 0)
				{
					serial_print("AHCI_ABAR_NONE\n");
					return;
				}

				{
					uint32_t off;

					for (off = 0;
					     off < AHCI_ABAR_MAP_PAGES *
							   PAGE_SIZE_4KB;
					     off += PAGE_SIZE_4KB)
					{
						uint64_t p =
							(uint64_t)ahci_abar_phys +
							off;

						if (map_page(p, p,
							     PAGE_PRESENT |
								     PAGE_RW |
								     PAGE_CACHE_DISABLE) !=
						    0)
						{
							serial_print(
								"AHCI_MAP_FAIL\n");
							return;
						}
					}
				}

				ahci_abar =
					(volatile struct hba_mem *)ahci_abar_phys;
				ahci_abar->ghc |= AHCI_GHC_AE;

				cap = ahci_abar->cap;
				ahci_hba_sncq = (cap & AHCI_CAP_SNCQ) ? 1 : 0;
				ahci_hba_ncs =
					(int)(((cap & AHCI_CAP_NCS_MASK) >>
					       AHCI_CAP_NCS_SHIFT) +
					      1);
				if (ahci_hba_ncs < 1)
					ahci_hba_ncs = 1;
				if (ahci_hba_ncs > (int)AHCI_CMD_SLOTS)
					ahci_hba_ncs = (int)AHCI_CMD_SLOTS;
				if (!ahci_hba_sncq)
					serial_print("AHCI_NCQ_UNSUPPORTED\n");

				pi = ahci_abar->pi;
				for (port_idx = 0; port_idx < 32; port_idx++)
				{
					struct ahci_slot *as;
					uint32_t ssts;
					uint32_t det;
					int saved_ncq;

					if (ahci_nslots >= AHCI_MAX_PORTS)
						break;
					if (!(pi & (1u << port_idx)))
						continue;
					ssts = ahci_abar->ports[port_idx].ssts;
					det = ssts & AHCI_SSTS_DET_MASK;
					if (det != AHCI_SSTS_DET_PRES &&
					    det != 0x01u)
						continue;

					as = &ahci_slots[ahci_nslots];
					as->port = &ahci_abar->ports[port_idx];
					memcpy(as->name, names[ahci_nslots], 4);
					if (ahci_port_rebase(as) != 0)
						continue;
					if (ahci_identify(as) != 0)
					{
						serial_print("AHCI_IDENT_FAIL\n");
						continue;
					}
					as->ready = 1;
					ahci_register_block(as);
					memset(as->io_buf, 0, sizeof(as->io_buf));
					/* First read via DMA EXT (ncq off). */
					saved_ncq = as->ncq_ok;
					as->ncq_ok = 0;
					if (ahci_backend_read(as, 0, 1,
							      as->io_buf) == 0)
					{
						if (ahci_nslots == 0)
							serial_print(
								"AHCI_READ_OK\n");
						as->ncq_ok = saved_ncq;
						if (ahci_hba_sncq &&
						    as->ncq_ok &&
						    ahci_nslots == 0)
						{
							memset(as->io_buf, 0,
							       sizeof(as->io_buf));
							if (ahci_backend_read(
								    as, 0, 1,
								    as->io_buf) ==
							    0)
								serial_print(
									"AHCI_NCQ_OK\n");
							else
							{
								serial_print(
									"AHCI_NCQ_FAIL\n");
								as->ncq_ok = 0;
							}
						}
						else if (ahci_hba_sncq &&
							 !as->ncq_ok &&
							 ahci_nslots == 0)
							serial_print(
								"AHCI_NCQ_UNSUPPORTED\n");
					}
					else
					{
						serial_print("AHCI_READ_FAIL\n");
						as->ncq_ok = 0;
					}

					ahci_nslots++;
				}
				if (ahci_nslots == 0)
					serial_print("AHCI_PORT_NONE\n");
				else if (ahci_nslots >= 2)
					serial_print("AHCI_MULTI_OK\n");
				return;
			}
		}
	}
	serial_print("AHCI_DETECT_NONE\n");
}
