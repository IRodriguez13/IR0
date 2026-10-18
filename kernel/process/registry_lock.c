/* SPDX-License-Identifier: GPL-3.0-only */
/* Ownership boundary for the global PID/process registry. */

#include "process_internal.h"

#if defined(CONFIG_SMP) && CONFIG_SMP
#error "process registry: SMP lock backend not implemented"
#endif

uint64_t process_registry_lock(void)
{
	return (uint64_t)irq_save();
}

void process_registry_unlock(uint64_t irq_flags)
{
	irq_restore((unsigned long)irq_flags);
}
