/* SPDX-License-Identifier: GPL-3.0-only */

#include "runqueue_lock.h"

#include <config.h>
#include <ir0/cpu.h>

#if defined(CONFIG_SMP) && CONFIG_SMP
#error "scheduler: SMP runqueue lock backend not implemented"
#endif

uint64_t sched_runqueue_lock(void)
{
	return (uint64_t)irq_save();
}

void sched_runqueue_unlock(uint64_t irq_flags)
{
	irq_restore((unsigned long)irq_flags);
}
