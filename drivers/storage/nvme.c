/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: nvme.c
 * Description: Minimal NVMe polling driver — admin + one I/O queue + read/write.
 *
 * Reference: https://wiki.osdev.org/NVMe (PCI 01:08, CAP/CC/CSTS, Identify, I/O).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/cpu.h>
#include "nvme.h"

#include <ir0/blockdev.h>
#include <ir0/errno.h>
#include <ir0/ktm/klog.h>
#include <mm/paging.h>
#include <string.h>
#include <stdint.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_CLASS_MASS     0x01
#define PCI_SUBCLASS_NVME  0x08

#define NVME_REG_CAP   0x00
#define NVME_REG_VS    0x08
#define NVME_REG_CC    0x14
#define NVME_REG_CSTS  0x1C
#define NVME_REG_AQA   0x24
#define NVME_REG_ASQ   0x28
#define NVME_REG_ACQ   0x30

#define NVME_CC_EN     (1u << 0)
#define NVME_CC_IOSQES (6u << 16)
#define NVME_CC_IOCQES (4u << 20)
#define NVME_CSTS_RDY  (1u << 0)

#define NVME_ADMIN_CREATE_SQ 0x01
#define NVME_ADMIN_CREATE_CQ 0x05
#define NVME_ADMIN_IDENTIFY  0x06
#define NVME_IO_WRITE        0x01
#define NVME_IO_READ         0x02

#define NVME_Q_DEPTH     16u
#define NVME_Q_MASK      (NVME_Q_DEPTH - 1u)
#define NVME_SQ_BYTES    (NVME_Q_DEPTH * 64u)
#define NVME_CQ_BYTES    (NVME_Q_DEPTH * 16u)
#define NVME_BAR_PAGES   8u
#define NVME_IDENT_BYTES 4096u

struct nvme_cmd
{
	uint32_t cdw0;
	uint32_t nsid;
	uint64_t rsvd;
	uint64_t mptr;
	uint64_t prp1;
	uint64_t prp2;
	uint32_t cdw10;
	uint32_t cdw11;
	uint32_t cdw12;
	uint32_t cdw13;
	uint32_t cdw14;
	uint32_t cdw15;
} __attribute__((packed));

struct nvme_cqe
{
	uint32_t dw0;
	uint32_t dw1;
	uint32_t dw2;
	uint32_t dw3;
} __attribute__((packed));

struct nvme_ctrl
{
	volatile uint8_t *bar;
	uintptr_t bar_phys;
	uint32_t doorbell_stride;
	uint32_t sector_size;
	uint64_t nsectors;
	uint16_t admin_sq_tail;
	uint16_t admin_cq_head;
	uint16_t admin_cq_phase;
	uint16_t io_sq_tail;
	uint16_t io_cq_head;
	uint16_t io_cq_phase;
	uint16_t next_cid;
	int ready;
	int registered;
	uint8_t asq_mem[4096] __attribute__((aligned(4096)));
	uint8_t acq_mem[4096] __attribute__((aligned(4096)));
	uint8_t iosq_mem[4096] __attribute__((aligned(4096)));
	uint8_t iocq_mem[4096] __attribute__((aligned(4096)));
	uint8_t ident[NVME_IDENT_BYTES] __attribute__((aligned(4096)));
	uint8_t io_buf[4096] __attribute__((aligned(4096)));
};

static struct nvme_ctrl g_nvme;

static struct nvme_cmd *nvme_asq(struct nvme_ctrl *c)
{
	return (struct nvme_cmd *)c->asq_mem;
}

static struct nvme_cqe *nvme_acq(struct nvme_ctrl *c)
{
	return (struct nvme_cqe *)c->acq_mem;
}

static struct nvme_cmd *nvme_iosq(struct nvme_ctrl *c)
{
	return (struct nvme_cmd *)c->iosq_mem;
}

static struct nvme_cqe *nvme_iocq(struct nvme_ctrl *c)
{
	return (struct nvme_cqe *)c->iocq_mem;
}

static uint32_t nvme_pci_read(uint8_t bus, uint8_t slot, uint8_t func,
			      uint8_t offset)
{
	uint32_t addr = (uint32_t)(1u << 31) | ((uint32_t)bus << 16) |
			((uint32_t)slot << 11) | ((uint32_t)func << 8) |
			(offset & 0xFCu);
	outl((uint16_t)PCI_CONFIG_ADDRESS, addr);
	uint32_t val;
	val = inl((uint16_t)PCI_CONFIG_DATA);
	return val;
}

static void nvme_pci_write(uint8_t bus, uint8_t slot, uint8_t func,
			   uint8_t offset, uint32_t val)
{
	uint32_t addr = (uint32_t)(1u << 31) | ((uint32_t)bus << 16) |
			((uint32_t)slot << 11) | ((uint32_t)func << 8) |
			(offset & 0xFCu);
	outl((uint16_t)PCI_CONFIG_ADDRESS, addr);
	outl((uint16_t)PCI_CONFIG_DATA, val);
}

static void nvme_delay(void)
{
	volatile int i;
	for (i = 0; i < 200; i++)
		;
}

static uint32_t nvme_rd32(volatile uint8_t *bar, uint32_t off)
{
	return *(volatile uint32_t *)(bar + off);
}

static uint64_t nvme_rd64(volatile uint8_t *bar, uint32_t off)
{
	return *(volatile uint64_t *)(bar + off);
}

static void nvme_wr32(volatile uint8_t *bar, uint32_t off, uint32_t v)
{
	*(volatile uint32_t *)(bar + off) = v;
}

static void nvme_wr64(volatile uint8_t *bar, uint32_t off, uint64_t v)
{
	*(volatile uint64_t *)(bar + off) = v;
}

static volatile uint32_t *nvme_sq_doorbell(struct nvme_ctrl *c, uint16_t qid)
{
	uint32_t off = 0x1000u + (uint32_t)(2u * qid) * c->doorbell_stride;
	return (volatile uint32_t *)(c->bar + off);
}

static volatile uint32_t *nvme_cq_doorbell(struct nvme_ctrl *c, uint16_t qid)
{
	uint32_t off = 0x1000u + (uint32_t)(2u * qid + 1u) * c->doorbell_stride;
	return (volatile uint32_t *)(c->bar + off);
}

static int nvme_wait_csts(struct nvme_ctrl *c, int want_rdy)
{
	uint32_t spin;

	for (spin = 0; spin < 2000000u; spin++)
	{
		uint32_t csts = nvme_rd32(c->bar, NVME_REG_CSTS);
		if (want_rdy)
		{
			if (csts & NVME_CSTS_RDY)
				return 0;
		}
		else if (!(csts & NVME_CSTS_RDY))
			return 0;
		nvme_delay();
	}
	return -EIO;
}

static int nvme_submit_admin(struct nvme_ctrl *c, struct nvme_cmd *cmd)
{
	uint16_t tail = c->admin_sq_tail;
	uint32_t spin;
	uint16_t cid = c->next_cid++;

	cmd->cdw0 = (cmd->cdw0 & 0xFFFFu) | ((uint32_t)cid << 16);
	nvme_asq(c)[tail] = *cmd;
	c->admin_sq_tail = (uint16_t)((tail + 1u) & NVME_Q_MASK);
	*nvme_sq_doorbell(c, 0) = c->admin_sq_tail;

	for (spin = 0; spin < 2000000u; spin++)
	{
		struct nvme_cqe *cqe = &nvme_acq(c)[c->admin_cq_head];
		uint16_t phase = (uint16_t)((cqe->dw3 >> 16) & 1u);
		uint16_t status;

		if (phase != c->admin_cq_phase)
		{
			nvme_delay();
			continue;
		}
		status = (uint16_t)((cqe->dw3 >> 17) & 0x7FFu);
		c->admin_cq_head = (uint16_t)((c->admin_cq_head + 1u) & NVME_Q_MASK);
		if (c->admin_cq_head == 0)
			c->admin_cq_phase ^= 1u;
		*nvme_cq_doorbell(c, 0) = c->admin_cq_head;
		if (status != 0)
			return -EIO;
		return 0;
	}
	return -EIO;
}

static int nvme_submit_io(struct nvme_ctrl *c, struct nvme_cmd *cmd)
{
	uint16_t tail = c->io_sq_tail;
	uint32_t spin;
	uint16_t cid = c->next_cid++;

	cmd->cdw0 = (cmd->cdw0 & 0xFFFFu) | ((uint32_t)cid << 16);
	nvme_iosq(c)[tail] = *cmd;
	c->io_sq_tail = (uint16_t)((tail + 1u) & NVME_Q_MASK);
	*nvme_sq_doorbell(c, 1) = c->io_sq_tail;

	for (spin = 0; spin < 2000000u; spin++)
	{
		struct nvme_cqe *cqe = &nvme_iocq(c)[c->io_cq_head];
		uint16_t phase = (uint16_t)((cqe->dw3 >> 16) & 1u);
		uint16_t status;

		if (phase != c->io_cq_phase)
		{
			nvme_delay();
			continue;
		}
		status = (uint16_t)((cqe->dw3 >> 17) & 0x7FFu);
		c->io_cq_head = (uint16_t)((c->io_cq_head + 1u) & NVME_Q_MASK);
		if (c->io_cq_head == 0)
			c->io_cq_phase ^= 1u;
		*nvme_cq_doorbell(c, 1) = c->io_cq_head;
		if (status != 0)
			return -EIO;
		return 0;
	}
	return -EIO;
}

static int nvme_identify(struct nvme_ctrl *c, uint32_t nsid, uint32_t cns)
{
	struct nvme_cmd cmd;

	memset(&cmd, 0, sizeof(cmd));
	memset(c->ident, 0, sizeof(c->ident));
	cmd.cdw0 = NVME_ADMIN_IDENTIFY;
	cmd.nsid = nsid;
	cmd.prp1 = (uint64_t)(uintptr_t)c->ident;
	cmd.cdw10 = cns;
	return nvme_submit_admin(c, &cmd);
}

static int nvme_create_io_queues(struct nvme_ctrl *c)
{
	struct nvme_cmd cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_CREATE_CQ;
	cmd.prp1 = (uint64_t)(uintptr_t)nvme_iocq(c);
	cmd.cdw10 = ((NVME_Q_DEPTH - 1u) << 16) | 1u; /* qid=1 */
	cmd.cdw11 = 1u; /* physically contiguous */
	ret = nvme_submit_admin(c, &cmd);
	if (ret < 0)
		return ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = NVME_ADMIN_CREATE_SQ;
	cmd.prp1 = (uint64_t)(uintptr_t)nvme_iosq(c);
	cmd.cdw10 = ((NVME_Q_DEPTH - 1u) << 16) | 1u;
	cmd.cdw11 = (1u << 16) | 1u; /* cqid=1, PC */
	return nvme_submit_admin(c, &cmd);
}

static int nvme_rw(struct nvme_ctrl *c, uint64_t lba, uint32_t nlb, void *buf,
		   int write)
{
	struct nvme_cmd cmd;
	uint32_t bytes;
	int ret;

	if (!c->ready || !buf || nlb == 0)
		return -EINVAL;
	bytes = nlb * c->sector_size;
	if (bytes > sizeof(c->io_buf))
		return -EINVAL;

	if (write)
		memcpy(c->io_buf, buf, bytes);
	else
		memset(c->io_buf, 0, bytes);

	memset(&cmd, 0, sizeof(cmd));
	cmd.cdw0 = write ? NVME_IO_WRITE : NVME_IO_READ;
	cmd.nsid = 1;
	cmd.prp1 = (uint64_t)(uintptr_t)c->io_buf;
	cmd.cdw10 = (uint32_t)(lba & 0xFFFFFFFFu);
	cmd.cdw11 = (uint32_t)(lba >> 32);
	cmd.cdw12 = nlb - 1u;
	ret = nvme_submit_io(c, &cmd);
	if (ret < 0)
		return ret;
	if (!write)
		memcpy(buf, c->io_buf, bytes);
	return 0;
}

static int nvme_backend_read(void *ctx, uint64_t lba, uint32_t count, void *buf)
{
	struct nvme_ctrl *c = ctx;
	uint8_t *dst = buf;
	uint32_t i;
	int ret;

	if (!c || !c->ready || !buf || count == 0)
		return -EINVAL;
	for (i = 0; i < count; i++)
	{
		ret = nvme_rw(c, lba + i, 1, c->io_buf, 0);
		if (ret < 0)
			return ret;
		memcpy(dst + (size_t)i * c->sector_size, c->io_buf, c->sector_size);
	}
	return 0;
}

static int nvme_backend_write(void *ctx, uint64_t lba, uint32_t count,
			      const void *buf)
{
	struct nvme_ctrl *c = ctx;
	const uint8_t *src = buf;
	uint32_t i;
	int ret;

	if (!c || !c->ready || !buf || count == 0)
		return -EINVAL;
	for (i = 0; i < count; i++)
	{
		memcpy(c->io_buf, src + (size_t)i * c->sector_size, c->sector_size);
		ret = nvme_rw(c, lba + i, 1, c->io_buf, 1);
		if (ret < 0)
			return ret;
	}
	return 0;
}

static const struct ir0_block_ops nvme_block_ops = {
	.read = nvme_backend_read,
	.write = nvme_backend_write,
	.flush = NULL,
};

static void nvme_register_block(struct nvme_ctrl *c)
{
	struct ir0_block_device dev;

	if (!c || c->registered || !c->ready)
		return;
	memset(&dev, 0, sizeof(dev));
	dev.ctx = c;
	dev.ops = &nvme_block_ops;
	dev.info.sector_size = c->sector_size;
	dev.info.max_sectors_per_io = 1;
	dev.info.sector_count = c->nsectors;
	dev.info.flags = IR0_BLOCK_FLAG_DMA_CAPABLE;
	memcpy(dev.info.name, "nvme0", 6);
	if (ir0_block_register(&dev) == 0)
		c->registered = 1;
}

static int nvme_controller_init(struct nvme_ctrl *c)
{
	uint64_t cap;
	uint32_t aqa;
	uint32_t cc;
	uint32_t lba_ds;
	uint64_t nsze;
	int ret;

	cap = nvme_rd64(c->bar, NVME_REG_CAP);
	c->doorbell_stride = (uint32_t)(4u << ((cap >> 32) & 0xFu));

	/* Disable controller. */
	cc = nvme_rd32(c->bar, NVME_REG_CC);
	cc &= ~NVME_CC_EN;
	nvme_wr32(c->bar, NVME_REG_CC, cc);
	if (nvme_wait_csts(c, 0) < 0)
	{
		klog_smoke("NVME_DISABLE_FAIL");
		return -EIO;
	}

	memset(c->asq_mem, 0, sizeof(c->asq_mem));
	memset(c->acq_mem, 0, sizeof(c->acq_mem));
	memset(c->iosq_mem, 0, sizeof(c->iosq_mem));
	memset(c->iocq_mem, 0, sizeof(c->iocq_mem));
	c->admin_sq_tail = 0;
	c->admin_cq_head = 0;
	c->admin_cq_phase = 1;
	c->io_sq_tail = 0;
	c->io_cq_head = 0;
	c->io_cq_phase = 1;
	c->next_cid = 1;

	aqa = ((NVME_Q_DEPTH - 1u) << 16) | (NVME_Q_DEPTH - 1u);
	nvme_wr32(c->bar, NVME_REG_AQA, aqa);
	nvme_wr64(c->bar, NVME_REG_ASQ, (uint64_t)(uintptr_t)c->asq_mem);
	nvme_wr64(c->bar, NVME_REG_ACQ, (uint64_t)(uintptr_t)c->acq_mem);

	cc = NVME_CC_IOSQES | NVME_CC_IOCQES | NVME_CC_EN;
	nvme_wr32(c->bar, NVME_REG_CC, cc);
	if (nvme_wait_csts(c, 1) < 0)
	{
		klog_smoke("NVME_ENABLE_FAIL");
		return -EIO;
	}

	ret = nvme_identify(c, 0, 1); /* controller */
	if (ret < 0)
	{
		klog_smoke("NVME_IDENT_CTRL_FAIL");
		return ret;
	}

	ret = nvme_identify(c, 1, 0); /* namespace 1 */
	if (ret < 0)
	{
		klog_smoke("NVME_IDENT_NS_FAIL");
		return ret;
	}

	nsze = *(uint64_t *)(c->ident + 0);
	lba_ds = (uint32_t)(c->ident[26] & 0x0Fu); /* FLBAS low nibble index */
	/* LBAF0 at offset 128: bits 16:23 = DS (power-of-two byte size). */
	{
		uint32_t lbaf0 = *(uint32_t *)(c->ident + 128);
		uint32_t ds = (lbaf0 >> 16) & 0xFFu;

		(void)lba_ds;
		c->sector_size = 1u << ds;
		if (c->sector_size < 512u || c->sector_size > 4096u)
			c->sector_size = 512u;
	}
	c->nsectors = nsze ? nsze : 1;
	klog_smoke("NVME_IDENT_OK");

	ret = nvme_create_io_queues(c);
	if (ret < 0)
	{
		klog_smoke("NVME_IOQ_FAIL");
		return ret;
	}

	c->ready = 1;
	return 0;
}

int nvme_disk_present(void)
{
	return g_nvme.ready;
}

uint64_t nvme_sector_count(void)
{
	return g_nvme.ready ? g_nvme.nsectors : 0;
}

void nvme_probe(void)
{
	uint8_t bus, slot, func;
	uint32_t bar0, cmd;
	uint32_t off;

	memset(&g_nvme, 0, sizeof(g_nvme));

	for (bus = 0; bus < 8; bus++)
	{
		for (slot = 0; slot < 32; slot++)
		{
			for (func = 0; func < 8; func++)
			{
				uint32_t id = nvme_pci_read(bus, slot, func, 0);
				uint32_t class_rev;
				uint8_t pci_class, pci_subclass;

				if ((id & 0xFFFF) == 0xFFFF)
					continue;
				class_rev = nvme_pci_read(bus, slot, func, 0x08);
				pci_class = (uint8_t)((class_rev >> 24) & 0xFFU);
				pci_subclass =
					(uint8_t)((class_rev >> 16) & 0xFFU);
				if (pci_class != PCI_CLASS_MASS ||
				    pci_subclass != PCI_SUBCLASS_NVME)
					continue;

				klog_smoke("NVME_DETECT_OK");

				cmd = nvme_pci_read(bus, slot, func, 0x04);
				cmd |= 0x6; /* bus master + memory */
				nvme_pci_write(bus, slot, func, 0x04, cmd);

				bar0 = nvme_pci_read(bus, slot, func, 0x10);
				if ((bar0 & 0x6u) == 0x4u)
				{
					uint32_t bar1 = nvme_pci_read(bus, slot, func, 0x14);

					g_nvme.bar_phys =
						(uintptr_t)((bar0 & ~0xFull) |
							    ((uint64_t)bar1 << 32));
				}
				else
					g_nvme.bar_phys = (uintptr_t)(bar0 & ~0xFu);
				if (g_nvme.bar_phys == 0)
				{
					klog_smoke("NVME_BAR_NONE");
					return;
				}

				for (off = 0; off < NVME_BAR_PAGES * PAGE_SIZE_4KB;
				     off += PAGE_SIZE_4KB)
				{
					uint64_t p = (uint64_t)g_nvme.bar_phys + off;

					if (map_page(p, p,
						     PAGE_PRESENT | PAGE_RW |
							     PAGE_CACHE_DISABLE) !=
					    0)
					{
						klog_smoke("NVME_MAP_FAIL");
						return;
					}
				}

				g_nvme.bar = (volatile uint8_t *)g_nvme.bar_phys;
				if (nvme_controller_init(&g_nvme) != 0)
				{
					klog_smoke("NVME_INIT_FAIL");
					return;
				}

				nvme_register_block(&g_nvme);
				if (nvme_backend_read(&g_nvme, 0, 1, g_nvme.io_buf) ==
				    0)
					klog_smoke("NVME_READ_OK");
				else
					klog_smoke("NVME_READ_FAIL");
				return;
			}
		}
	}
	klog_smoke("NVME_DETECT_NONE");
}
