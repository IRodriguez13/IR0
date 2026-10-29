/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: resource_registry.h
 * Description: IRQ, I/O port, and MMIO resource registration facade.
 */

#pragma once

#include <ir0/errno.h>
#include <stdint.h>

void resource_register_irq(uint8_t irq, const char *name);
void resource_register_ioport(uint16_t start, uint16_t end, const char *name);
void resource_register_mmio(uint64_t start, uint64_t end, const char *name);

/*
 * Remove the exact entry registered earlier.  Returns 0 when the entry was
 * found and removed, -ENOENT otherwise.  Removal preserves registration
 * order and lets hotplug/teardown paths (and tests) stop exporting a range.
 */
int resource_unregister_irq(uint8_t irq, const char *name);
int resource_unregister_ioport(uint16_t start, uint16_t end, const char *name);
int resource_unregister_mmio(uint64_t start, uint64_t end, const char *name);

void resource_foreach_irq(int (*cb)(uint8_t irq, const char *name, void *ctx),
			  void *ctx);
void resource_foreach_ioport(int (*cb)(uint16_t start, uint16_t end,
				       const char *name, void *ctx),
			     void *ctx);
void resource_foreach_mmio(int (*cb)(uint64_t start, uint64_t end,
				     const char *name, void *ctx),
			   void *ctx);
