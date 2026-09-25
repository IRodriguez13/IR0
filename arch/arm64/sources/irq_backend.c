/** IR0 ARM64 firmware-selected interrupt-controller facade. */
/* SPDX-License-Identifier: GPL-3.0-only */

#include "irq_backend.h"
#include "gic_v2.h"

struct arm64_irq_ops
{
	int (*init)(void);
	int (*enable)(uint32_t irq);
	uint32_t (*ack)(void);
	void (*eoi)(uint32_t token);
};

static const struct arm64_irq_ops g_gic_v2_ops = {
	arm64_gic_v2_init,
	arm64_gic_v2_enable,
	arm64_gic_v2_ack,
	arm64_gic_v2_eoi,
};
static const struct arm64_irq_ops *g_ops;

int arm64_irq_backend_select(enum arm64_irq_controller_model model,
			     const struct ir0_phys_range *ranges,
			     uint32_t range_count)
{
	if (model != ARM64_IRQ_CONTROLLER_GIC_V2 || !ranges || range_count < 2U)
		return -1;
	if (arm64_gic_v2_configure(ranges[0].base, ranges[0].size,
				   ranges[1].base, ranges[1].size) != 0)
		return -1;
	g_ops = &g_gic_v2_ops;
	return 0;
}

int arm64_irq_backend_init(void)
{
	return g_ops ? g_ops->init() : -1;
}

int arm64_irq_backend_enable(uint32_t irq)
{
	return g_ops ? g_ops->enable(irq) : -1;
}

uint32_t arm64_irq_backend_ack(void)
{
	return g_ops ? g_ops->ack() : 1023U;
}

void arm64_irq_backend_eoi(uint32_t token)
{
	if (g_ops)
		g_ops->eoi(token);
}
