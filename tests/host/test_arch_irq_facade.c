/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * ARCH-5 host contract: irq_save / irq_restore nesting.
 */

#include "test_harness.h"

unsigned long irq_save(void);
void irq_restore(unsigned long flags);
unsigned long host_arch_irq_depth(void);
void host_arch_irq_reset(void);

void test_arch_irq_facade_nested(void)
{
	unsigned long a;
	unsigned long b;

	TEST_BEGIN("arch_irq_facade_nested");

	host_arch_irq_reset();
	ASSERT_EQ(0UL, host_arch_irq_depth());

	a = irq_save();
	ASSERT_EQ(1UL, host_arch_irq_depth());
	ASSERT_EQ(0UL, a);

	b = irq_save();
	ASSERT_EQ(2UL, host_arch_irq_depth());
	ASSERT_EQ(1UL, b);

	irq_restore(b);
	ASSERT_EQ(1UL, host_arch_irq_depth());

	irq_restore(a);
	ASSERT_EQ(0UL, host_arch_irq_depth());

	TEST_END();
}
