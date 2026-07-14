/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: virtio_mmio.h
 * Description: Freestanding virtio-mmio probe (QEMU ARM virt @ 0x0a000000).
 */

#pragma once

#include <stdint.h>

#define VIRTIO_MMIO_BASE       0x0a000000UL
#define VIRTIO_MMIO_STRIDE     0x200UL
#define VIRTIO_MMIO_MAX_SLOTS  32U

#define VIRTIO_MMIO_MAGIC      0x74726976U /* "virt" LE */

#define VIRTIO_ID_NET   1U
#define VIRTIO_ID_BLOCK 2U

#define VIRTIO_MMIO_REG_MAGIC          0x000U
#define VIRTIO_MMIO_REG_VERSION        0x004U
#define VIRTIO_MMIO_REG_DEVICE_ID      0x008U
#define VIRTIO_MMIO_REG_VENDOR_ID      0x00cU
#define VIRTIO_MMIO_REG_DEVICE_FEATURES 0x010U
#define VIRTIO_MMIO_REG_DEVICE_FEATURES_SEL 0x014U
#define VIRTIO_MMIO_REG_DRIVER_FEATURES 0x020U
#define VIRTIO_MMIO_REG_DRIVER_FEATURES_SEL 0x024U
/* Shared queue regs (legacy + modern) — Linux uapi virtio_mmio.h */
#define VIRTIO_MMIO_REG_QUEUE_SEL      0x030U
#define VIRTIO_MMIO_REG_QUEUE_NUM_MAX  0x034U
#define VIRTIO_MMIO_REG_QUEUE_NUM      0x038U
#define VIRTIO_MMIO_REG_QUEUE_READY    0x044U /* modern */
#define VIRTIO_MMIO_REG_QUEUE_NOTIFY   0x050U
#define VIRTIO_MMIO_REG_INTERRUPT_STATUS 0x060U
#define VIRTIO_MMIO_REG_INTERRUPT_ACK  0x064U
#define VIRTIO_MMIO_REG_STATUS         0x070U
#define VIRTIO_MMIO_REG_QUEUE_DESC_LOW 0x080U
#define VIRTIO_MMIO_REG_QUEUE_DESC_HIGH 0x084U
#define VIRTIO_MMIO_REG_QUEUE_DRIVER_LOW 0x090U
#define VIRTIO_MMIO_REG_QUEUE_DRIVER_HIGH 0x094U
#define VIRTIO_MMIO_REG_QUEUE_DEVICE_LOW 0x0a0U
#define VIRTIO_MMIO_REG_QUEUE_DEVICE_HIGH 0x0a4U
#define VIRTIO_MMIO_REG_CONFIG         0x100U

#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER      2U
#define VIRTIO_STATUS_DRIVER_OK   4U
#define VIRTIO_STATUS_FEATURES_OK 8U
#define VIRTIO_STATUS_FAILED      128U

struct virtio_mmio_dev
{
	volatile uint32_t *base;
	uint32_t version;
	uint32_t device_id;
	uint32_t vendor_id;
	int slot;
};

/** Map virtio-mmio window and scan transports; prints ARM64_VIRTIO_MMIO_OK. */
int arm64_virtio_mmio_probe(void);

/** First discovered device of @device_id, or NULL. */
struct virtio_mmio_dev *arm64_virtio_mmio_find(uint32_t device_id);

uint32_t virtio_mmio_read(struct virtio_mmio_dev *d, uint32_t off);
void virtio_mmio_write(struct virtio_mmio_dev *d, uint32_t off, uint32_t val);
void virtio_mmio_set_status(struct virtio_mmio_dev *d, uint32_t status);
uint32_t virtio_mmio_get_status(struct virtio_mmio_dev *d);
