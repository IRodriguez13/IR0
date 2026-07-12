/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_9p.c
 * Description: Minimal virtio-pci legacy + 9P2000.L for QEMU host directory share.
 *
 * Smoke expects:
 *   -fsdev local,id=ir0fs,path=...,security_model=none
 *   -device virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on
 *
 * Sources: virtio 0.9.5 legacy PCI; Linux include/net/9p/9p.h message ids;
 * QEMU wiki Documentation/9psetup.
 */

#include "virtio_9p.h"

#include <interrupt/arch/io.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/serial_io.h>
#include <string.h>
#include <stddef.h>

#define PCI_CONFIG_ADDRESS 0xCF8
#define PCI_CONFIG_DATA    0xCFC
#define PCI_ENABLE_BIT     0x80000000U

#define VIRTIO_VENDOR      0x1AF4
#define VIRTIO_DEV_9P      0x1009

#define VIRTIO_PCI_HOST_FEATURES 0
#define VIRTIO_PCI_GUEST_FEATURES 4
#define VIRTIO_PCI_QUEUE_PFN     8
#define VIRTIO_PCI_QUEUE_NUM     12
#define VIRTIO_PCI_QUEUE_SEL     14
#define VIRTIO_PCI_QUEUE_NOTIFY  16
#define VIRTIO_PCI_STATUS        18
#define VIRTIO_PCI_ISR           19
#define VIRTIO_PCI_CONFIG        20

#define VIRTIO_STATUS_ACK        1
#define VIRTIO_STATUS_DRIVER     2
#define VIRTIO_STATUS_DRIVER_OK  4
#define VIRTIO_STATUS_FEATURES_OK 8
#define VIRTIO_STATUS_FAILED     128

#define VRING_DESC_F_NEXT  1
#define VRING_DESC_F_WRITE 2

/* 9P2000.L message ids (Linux include/net/9p/9p.h) */
#define P9_RLERROR  7
#define P9_TLOPEN   12
#define P9_RLOPEN   13
#define P9_TLCREATE 14
#define P9_RLCREATE 15
#define P9_TVERSION 100
#define P9_RVERSION 101
#define P9_TATTACH  104
#define P9_RATTACH  105
#define P9_RERROR   107
#define P9_TWALK    110
#define P9_RWALK    111
#define P9_TREAD    116
#define P9_RREAD    117
#define P9_TWRITE   118
#define P9_RWRITE   119
#define P9_TCLUNK   120
#define P9_RCLUNK   121

/* 9P2000.L open flags (match Linux O_* / P9_DOTL_*) */
#define P9_DOTL_RDONLY 0x00000000u
#define P9_DOTL_WRONLY 0x00000001u
#define P9_DOTL_RDWR   0x00000002u
#define P9_DOTL_CREAT  0x00000040u
#define P9_DOTL_TRUNC  0x00000200u

#define V9P_QUEUE_MAX  128
#define V9P_BUF_SIZE   8192
#define V9P_MSIZE      8192

struct vring_desc
{
	uint64_t addr;
	uint32_t len;
	uint16_t flags;
	uint16_t next;
};

struct vring_used_elem
{
	uint32_t id;
	uint32_t len;
};

static uint16_t g_iobase;
static int g_ready;
static uint16_t g_qsz;
static uint16_t g_last_used_idx;
static uint16_t g_p9_tag = 1;
static uint32_t g_root_fid = 1;

static struct vring_desc *g_desc;
static uint16_t *g_avail_flags;
static uint16_t *g_avail_idx;
static uint16_t *g_avail_ring;
static uint16_t *g_used_flags;
static uint16_t *g_used_idx;
static struct vring_used_elem *g_used_ring;
static uint8_t *g_req;
static uint8_t *g_resp;
static void *g_vring_mem;

static uint32_t pci_read(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	return inl(PCI_CONFIG_DATA);
}

static void pci_write(uint8_t bus, uint8_t slot, uint8_t func, uint8_t off, uint32_t val)
{
	uint32_t addr = PCI_ENABLE_BIT | ((uint32_t)bus << 16) | ((uint32_t)slot << 11) |
			((uint32_t)func << 8) | (off & 0xFCu);
	outl(PCI_CONFIG_ADDRESS, addr);
	outl(PCI_CONFIG_DATA, val);
}

static void vp_outb(uint16_t off, uint8_t v)
{
	outb((uint16_t)(g_iobase + off), v);
}

static void vp_outw(uint16_t off, uint16_t v)
{
	outw((uint16_t)(g_iobase + off), v);
}

static void vp_outl(uint16_t off, uint32_t v)
{
	outl((uint16_t)(g_iobase + off), v);
}

static uint8_t vp_inb(uint16_t off)
{
	return inb((uint16_t)(g_iobase + off));
}

static uint16_t vp_inw(uint16_t off)
{
	return inw((uint16_t)(g_iobase + off));
}

static uint32_t vp_inl(uint16_t off)
{
	return inl((uint16_t)(g_iobase + off));
}

static void p9_put8(uint8_t **p, uint8_t v)
{
	*(*p)++ = v;
}

static void p9_put16(uint8_t **p, uint16_t v)
{
	*(*p)++ = (uint8_t)(v & 0xFF);
	*(*p)++ = (uint8_t)((v >> 8) & 0xFF);
}

static void p9_put32(uint8_t **p, uint32_t v)
{
	*(*p)++ = (uint8_t)(v & 0xFF);
	*(*p)++ = (uint8_t)((v >> 8) & 0xFF);
	*(*p)++ = (uint8_t)((v >> 16) & 0xFF);
	*(*p)++ = (uint8_t)((v >> 24) & 0xFF);
}

static void p9_put64(uint8_t **p, uint64_t v)
{
	p9_put32(p, (uint32_t)(v & 0xFFFFFFFFu));
	p9_put32(p, (uint32_t)(v >> 32));
}

static void p9_putstr(uint8_t **p, const char *s)
{
	uint16_t n = (uint16_t)strlen(s);
	p9_put16(p, n);
	memcpy(*p, s, n);
	*p += n;
}

static uint8_t p9_get8(uint8_t **p)
{
	return *(*p)++;
}

static uint16_t p9_get16(uint8_t **p)
{
	uint16_t v = (uint16_t)(*p)[0] | ((uint16_t)(*p)[1] << 8);
	*p += 2;
	return v;
}

static uint32_t p9_get32(uint8_t **p)
{
	uint32_t v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
		     ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
	*p += 4;
	return v;
}

static int v9p_exchange(uint32_t req_len, uint32_t *resp_len)
{
	uint16_t desc_idx = 0;
	uint16_t avail_idx;
	int spins;

	if (!g_ready || !g_desc || !g_qsz)
		return -ENODEV;

	g_desc[0].addr = (uint64_t)(uintptr_t)g_req;
	g_desc[0].len = req_len;
	g_desc[0].flags = VRING_DESC_F_NEXT;
	g_desc[0].next = 1;

	g_desc[1].addr = (uint64_t)(uintptr_t)g_resp;
	g_desc[1].len = V9P_BUF_SIZE;
	g_desc[1].flags = VRING_DESC_F_WRITE;
	g_desc[1].next = 0;

	avail_idx = *g_avail_idx;
	g_avail_ring[avail_idx % g_qsz] = desc_idx;
	__asm__ volatile("mfence" ::: "memory");
	*g_avail_idx = (uint16_t)(avail_idx + 1);
	__asm__ volatile("mfence" ::: "memory");

	vp_outw(VIRTIO_PCI_QUEUE_NOTIFY, 0);

	for (spins = 0; spins < 2000000; spins++)
	{
		__asm__ volatile("mfence" ::: "memory");
		if (*g_used_idx != g_last_used_idx)
		{
			uint16_t u = (uint16_t)(g_last_used_idx % g_qsz);
			if (resp_len)
				*resp_len = g_used_ring[u].len;
			g_last_used_idx++;
			(void)vp_inb(VIRTIO_PCI_ISR);
			return 0;
		}
	}
	serial_print("HOSTSHARE_9P_NOTIFY_TIMEOUT\n");
	return -EIO;
}

static int v9p_rpc(uint8_t type, uint8_t *body, uint32_t body_len,
		   uint8_t expect_type, uint8_t **out_body, uint32_t *out_body_len)
{
	uint8_t *p = g_req;
	uint32_t size = 7 + body_len;
	uint32_t resp_len = 0;
	uint8_t *rp;
	uint32_t rsize;
	uint8_t rtype;
	uint16_t rtag;
	int rc;

	if (size > V9P_BUF_SIZE)
		return -EFBIG;

	p9_put32(&p, size);
	p9_put8(&p, type);
	p9_put16(&p, g_p9_tag);
	if (body_len)
		memcpy(p, body, body_len);

	rc = v9p_exchange(size, &resp_len);
	if (rc < 0)
		return rc;
	if (resp_len < 7)
		return -EIO;

	rp = g_resp;
	rsize = p9_get32(&rp);
	rtype = p9_get8(&rp);
	rtag = p9_get16(&rp);
	(void)rsize;
	(void)rtag;
	g_p9_tag++;

	if (rtype == P9_RERROR || rtype == P9_RLERROR)
		return -EIO;
	if (rtype != expect_type)
		return -EIO;

	if (out_body)
		*out_body = rp;
	if (out_body_len)
		*out_body_len = resp_len - 7;
	return 0;
}

static int v9p_version(void)
{
	uint8_t body[64];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	p9_put32(&p, V9P_MSIZE);
	p9_putstr(&p, "9P2000.L");
	rc = v9p_rpc(P9_TVERSION, body, (uint32_t)(p - body), P9_RVERSION, &rb, &rblen);
	return rc;
}

static int v9p_attach(uint32_t fid)
{
	uint8_t body[96];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	p9_put32(&p, (~0U)); /* afid NOFID */
	p9_putstr(&p, "");
	p9_putstr(&p, "");
	p9_put32(&p, 0); /* n_uname */
	return v9p_rpc(P9_TATTACH, body, (uint32_t)(p - body), P9_RATTACH, &rb, &rblen);
}

static int v9p_clunk(uint32_t fid)
{
	uint8_t body[8];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	return v9p_rpc(P9_TCLUNK, body, (uint32_t)(p - body), P9_RCLUNK, &rb, &rblen);
}

static int v9p_walk(uint32_t fid, uint32_t newfid, const char *name)
{
	uint8_t body[96];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	p9_put32(&p, fid);
	p9_put32(&p, newfid);
	if (name && name[0])
	{
		p9_put16(&p, 1);
		p9_putstr(&p, name);
	}
	else
	{
		p9_put16(&p, 0);
	}
	return v9p_rpc(P9_TWALK, body, (uint32_t)(p - body), P9_RWALK, &rb, &rblen);
}

static int v9p_lopen(uint32_t fid, uint32_t flags)
{
	uint8_t body[12];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tlopen: fid + flags (u32) — Linux p9_client_open "dd" */
	p9_put32(&p, fid);
	p9_put32(&p, flags);
	return v9p_rpc(P9_TLOPEN, body, (uint32_t)(p - body), P9_RLOPEN, &rb, &rblen);
}

static int v9p_lcreate(uint32_t fid, const char *name, uint32_t flags, uint32_t mode,
		       uint32_t gid)
{
	uint8_t body[128];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;

	/* Tlcreate: fid, name, flags, mode, gid — Linux "dsddg" */
	p9_put32(&p, fid);
	p9_putstr(&p, name);
	p9_put32(&p, flags);
	p9_put32(&p, mode);
	p9_put32(&p, gid);
	return v9p_rpc(P9_TLCREATE, body, (uint32_t)(p - body), P9_RLCREATE, &rb, &rblen);
}

static int v9p_write(uint32_t fid, uint64_t offset, const void *data, uint32_t count,
		     uint32_t *written)
{
	uint8_t body[V9P_BUF_SIZE];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	int rc;

	if (count + 16 > sizeof(body))
		return -EFBIG;
	p9_put32(&p, fid);
	p9_put64(&p, offset);
	p9_put32(&p, count);
	memcpy(p, data, count);
	p += count;
	rc = v9p_rpc(P9_TWRITE, body, (uint32_t)(p - body), P9_RWRITE, &rb, &rblen);
	if (rc < 0)
		return rc;
	if (rblen < 4)
		return -EIO;
	if (written)
		*written = p9_get32(&rb);
	return 0;
}

static int v9p_read(uint32_t fid, uint64_t offset, void *data, uint32_t count, uint32_t *got)
{
	uint8_t body[24];
	uint8_t *p = body;
	uint8_t *rb;
	uint32_t rblen;
	uint32_t n;
	int rc;

	p9_put32(&p, fid);
	p9_put64(&p, offset);
	p9_put32(&p, count);
	rc = v9p_rpc(P9_TREAD, body, (uint32_t)(p - body), P9_RREAD, &rb, &rblen);
	if (rc < 0)
		return rc;
	if (rblen < 4)
		return -EIO;
	n = p9_get32(&rb);
	if (n > count)
		n = count;
	if (n && data)
		memcpy(data, rb, n);
	if (got)
		*got = n;
	return 0;
}

static int find_virtio_9p(uint8_t *bus, uint8_t *slot)
{
	uint16_t b;
	uint8_t s;

	for (b = 0; b < 256; b++)
	{
		for (s = 0; s < 32; s++)
		{
			uint32_t id = pci_read((uint8_t)b, s, 0, 0);
			if ((id & 0xFFFF) == VIRTIO_VENDOR && (id >> 16) == VIRTIO_DEV_9P)
			{
				*bus = (uint8_t)b;
				*slot = s;
				return 0;
			}
		}
	}
	return -1;
}

static int setup_vring(uint16_t qsz)
{
	size_t desc_sz;
	size_t avail_sz;
	size_t used_off;
	size_t need;
	uintptr_t base;

	if (qsz == 0 || qsz > V9P_QUEUE_MAX)
		return -EINVAL;

	g_qsz = qsz;
	desc_sz = sizeof(struct vring_desc) * qsz;
	/* flags + idx + ring[qsz] + used_event (legacy layout) */
	avail_sz = 6u + 2u * qsz;
	used_off = (desc_sz + avail_sz + 4095u) & ~(size_t)4095u;
	need = used_off + 6u + sizeof(struct vring_used_elem) * qsz;
	need = (need + 4095u) & ~(size_t)4095u;

	g_vring_mem = kmalloc_aligned(need, 4096);
	if (!g_vring_mem)
		return -ENOMEM;
	memset(g_vring_mem, 0, need);
	base = (uintptr_t)g_vring_mem;
	g_desc = (struct vring_desc *)base;
	g_avail_flags = (uint16_t *)(base + desc_sz);
	g_avail_idx = g_avail_flags + 1;
	g_avail_ring = g_avail_idx + 1;
	g_used_flags = (uint16_t *)(base + used_off);
	g_used_idx = g_used_flags + 1;
	g_used_ring = (struct vring_used_elem *)(g_used_idx + 1);

	g_req = (uint8_t *)kmalloc_aligned(V9P_BUF_SIZE, 16);
	g_resp = (uint8_t *)kmalloc_aligned(V9P_BUF_SIZE, 16);
	if (!g_req || !g_resp)
		return -ENOMEM;
	memset(g_req, 0, V9P_BUF_SIZE);
	memset(g_resp, 0, V9P_BUF_SIZE);
	return 0;
}

int virtio_9p_init(void)
{
	uint8_t bus, slot;
	uint32_t bar0;
	uint32_t cmd;
	uint16_t qsz;
	uintptr_t pfn;
	int rc;

	g_ready = 0;
	if (find_virtio_9p(&bus, &slot) != 0)
	{
		serial_print("HOSTSHARE_9P_ABSENT\n");
		return -ENODEV;
	}

	cmd = pci_read(bus, slot, 0, 0x04);
	pci_write(bus, slot, 0, 0x04, cmd | 0x5); /* IO + BusMaster */

	bar0 = pci_read(bus, slot, 0, 0x10);
	if (!(bar0 & 1))
	{
		serial_print("HOSTSHARE_9P_NO_IOBAR\n");
		return -ENODEV;
	}
	g_iobase = (uint16_t)(bar0 & ~0x3u);

	vp_outb(VIRTIO_PCI_STATUS, 0);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK);
	vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER);

	(void)vp_inl(VIRTIO_PCI_HOST_FEATURES);
	vp_outl(VIRTIO_PCI_GUEST_FEATURES, 0);

	vp_outw(VIRTIO_PCI_QUEUE_SEL, 0);
	qsz = vp_inw(VIRTIO_PCI_QUEUE_NUM);
	if (qsz == 0 || qsz > V9P_QUEUE_MAX)
	{
		serial_print("HOSTSHARE_9P_BAD_QSZ\n");
		vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
		return -EINVAL;
	}

	rc = setup_vring(qsz);
	if (rc < 0)
	{
		vp_outb(VIRTIO_PCI_STATUS, VIRTIO_STATUS_FAILED);
		return rc;
	}

	/* Queue PFN: physical page of descriptor table (identity map). */
	pfn = (uintptr_t)g_desc >> 12;
	vp_outl(VIRTIO_PCI_QUEUE_PFN, (uint32_t)pfn);

	vp_outb(VIRTIO_PCI_STATUS,
		VIRTIO_STATUS_ACK | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);

	g_last_used_idx = 0;
	g_ready = 1;

	rc = v9p_version();
	if (rc < 0)
	{
		serial_print("HOSTSHARE_9P_VERSION_FAIL\n");
		g_ready = 0;
		return rc;
	}
	rc = v9p_attach(g_root_fid);
	if (rc < 0)
	{
		serial_print("HOSTSHARE_9P_ATTACH_FAIL\n");
		g_ready = 0;
		return rc;
	}

	serial_print("HOSTSHARE_9P_READY\n");
	return 0;
}

int virtio_9p_ready(void)
{
	return g_ready;
}

int virtio_9p_write_file(const char *relpath, const void *buf, size_t len)
{
	uint32_t fid = 2;
	uint32_t written = 0;
	int rc;
	const char *name = relpath;

	if (!g_ready || !relpath || !buf)
		return -EINVAL;
	while (*name == '/')
		name++;
	if (!name[0] || strchr(name, '/'))
		return -EINVAL; /* MVP: single-component path only */

	rc = v9p_walk(g_root_fid, fid, name);
	if (rc == 0)
	{
		rc = v9p_lopen(fid, P9_DOTL_WRONLY | P9_DOTL_TRUNC);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}
	else
	{
		/* clone root fid, then Tlcreate */
		rc = v9p_walk(g_root_fid, fid, NULL);
		if (rc < 0)
			return rc;
		rc = v9p_lcreate(fid, name, P9_DOTL_WRONLY, 0666u, 0u);
		if (rc < 0)
		{
			(void)v9p_clunk(fid);
			return rc;
		}
	}

	rc = v9p_write(fid, 0, buf, (uint32_t)len, &written);
	(void)v9p_clunk(fid);
	if (rc < 0)
		return rc;
	if (written != (uint32_t)len)
		return -EIO;
	return 0;
}

int virtio_9p_read_file(const char *relpath, void *buf, size_t maxlen)
{
	uint32_t fid = 3;
	uint32_t got = 0;
	int rc;
	const char *name = relpath;

	if (!g_ready || !relpath || !buf)
		return -EINVAL;
	while (*name == '/')
		name++;
	if (!name[0] || strchr(name, '/'))
		return -EINVAL;

	rc = v9p_walk(g_root_fid, fid, name);
	if (rc < 0)
		return rc;
	rc = v9p_lopen(fid, P9_DOTL_RDONLY);
	if (rc < 0)
	{
		(void)v9p_clunk(fid);
		return rc;
	}
	rc = v9p_read(fid, 0, buf, (uint32_t)maxlen, &got);
	(void)v9p_clunk(fid);
	if (rc < 0)
		return rc;
	return (int)got;
}
