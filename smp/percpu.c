/* SPDX-License-Identifier: GPL-3.0-only */
/** IR0 UP per-CPU storage; SMP allocation and ISA lookup land later. */

#include <ir0/cpu.h>
#include <ir0/errno.h>
#include <ir0/percpu.h>
#include <config.h>

#if defined(CONFIG_SMP) && CONFIG_SMP
#error "percpu: SMP storage backend not implemented"
#endif

static struct ir0_cpu_local boot_cpu;

struct ir0_cpu_local *cpu_local_by_id(unsigned int cpu)
{
	if (!cpu_is_possible(cpu))
		return NULL;
	boot_cpu.logical_id = 0;
	return &boot_cpu;
}

struct ir0_cpu_local *cpu_local(void)
{
	return cpu_local_by_id(cpu_id());
}

void cpu_local_set_current_task(struct task *task)
{
	cpu_local()->current_task = task;
}

void cpu_local_set_kernel_stack(uintptr_t stack_top)
{
	cpu_local()->kernel_stack_top = stack_top;
}

void cpu_local_irq_enter(void)
{
	struct ir0_cpu_local *cpu = cpu_local();
	cpu->irq_depth++;
	cpu->interrupts++;
}

int cpu_local_irq_leave(void)
{
	struct ir0_cpu_local *cpu = cpu_local();

	if (cpu->irq_depth == 0)
		return -EINVAL;
	cpu->irq_depth--;
	return 0;
}

void cpu_local_note_context_switch(void)
{
	cpu_local()->context_switches++;
}
