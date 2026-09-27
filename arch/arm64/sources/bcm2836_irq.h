/** IR0 ARM64 BCM2836 local interrupt-controller backend. */
/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

int arm64_bcm2836_irq_configure(uint64_t base, uint64_t size);
int arm64_bcm2836_irq_init(void);
int arm64_bcm2836_irq_enable(uint32_t irq);
uint32_t arm64_bcm2836_irq_ack(void);
void arm64_bcm2836_irq_eoi(uint32_t token);
