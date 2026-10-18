/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stddef.h>
#include <stdint.h>

struct task;

struct ir0_cpu_local
{
	unsigned int logical_id;
	struct task *current_task;
	uintptr_t kernel_stack_top;
	unsigned int irq_depth;
	uint64_t context_switches;
	uint64_t interrupts;
};

struct ir0_cpu_local *cpu_local(void);
struct ir0_cpu_local *cpu_local_by_id(unsigned int cpu);
void cpu_local_set_current_task(struct task *task);
void cpu_local_set_kernel_stack(uintptr_t stack_top);
void cpu_local_irq_enter(void);
int cpu_local_irq_leave(void);
void cpu_local_note_context_switch(void);
