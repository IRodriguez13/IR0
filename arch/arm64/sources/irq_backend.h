/** IR0 ARM64 private interrupt-controller facade. */
/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <ir0/arm64_board.h>
#include <stdint.h>

#define ARM64_IRQ_PHYS_TIMER 30U

int arm64_irq_backend_select(enum arm64_irq_controller_model model,
			     const struct ir0_phys_range *ranges,
			     uint32_t range_count);
int arm64_irq_backend_init(void);
int arm64_irq_backend_enable(uint32_t irq);
uint32_t arm64_irq_backend_ack(void);
void arm64_irq_backend_eoi(uint32_t token);

