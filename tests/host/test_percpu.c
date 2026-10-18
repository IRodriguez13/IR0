/* SPDX-License-Identifier: GPL-3.0-only */
#include "test_harness.h"
#include <ir0/errno.h>
#include <ir0/percpu.h>

void test_percpu_up_contract(void)
{
	struct ir0_cpu_local *cpu = cpu_local();
	int task_marker;

	TEST_BEGIN("percpu UP storage and nesting contract");
	ASSERT(cpu != NULL);
	ASSERT(cpu == cpu_local_by_id(0));
	ASSERT(cpu_local_by_id(1) == NULL);
	cpu_local_set_current_task((struct task *)&task_marker);
	ASSERT(cpu->current_task == (struct task *)&task_marker);
	cpu_local_set_kernel_stack(0x12345000U);
	ASSERT(cpu->kernel_stack_top == 0x12345000U);
	ASSERT(cpu_local_irq_leave() == -EINVAL);
	cpu_local_irq_enter();
	ASSERT(cpu->irq_depth == 1);
	cpu_local_irq_enter();
	ASSERT(cpu->irq_depth == 2);
	ASSERT(cpu->interrupts == 2);
	ASSERT(cpu_local_irq_leave() == 0);
	ASSERT(cpu->irq_depth == 1);
	ASSERT(cpu_local_irq_leave() == 0);
	ASSERT(cpu->irq_depth == 0);
	cpu_local_note_context_switch();
	ASSERT(cpu->context_switches == 1);
	TEST_END();
}
