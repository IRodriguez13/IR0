/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Resource registry (IRQ and I/O ports)
 * Copyright (C) 2025  Iván Rodriguez
 *
 * Drivers register their actual hardware resources here; /proc uses only
 * this data. No hardcoded IRQ or port names in procfs.
 */

#ifndef KERNEL_RESOURCE_REGISTRY_H
#define KERNEL_RESOURCE_REGISTRY_H

#include <stdint.h>

/*
 * Register an IRQ with the name reported by the driver/hardware layer.
 * Called from driver init (e.g. ATA, RTL8139, PIT, PS2).
 */
void resource_register_irq(uint8_t irq, const char *name);

/*
 * Register an I/O port range [start, end] with the name from the driver.
 * Port numbers must come from driver headers (e.g. ATA_PRIMARY_DATA).
 */
void resource_register_ioport(uint16_t start, uint16_t end, const char *name);

/*
 * Iterate registered IRQs. Callback receives (irq, name). Stop if callback returns non-zero.
 */
void resource_foreach_irq(int (*cb)(uint8_t irq, const char *name, void *ctx), void *ctx);

/*
 * Iterate registered I/O port ranges. Callback receives (start, end, name). Stop if callback returns non-zero.
 */
void resource_foreach_ioport(int (*cb)(uint16_t start, uint16_t end, const char *name, void *ctx), void *ctx);

#endif /* KERNEL_RESOURCE_REGISTRY_H */
