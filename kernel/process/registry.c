/* SPDX-License-Identifier: GPL-3.0-only */
/* Single owner for the architecture-neutral process registry state. */

#include "process_internal.h"

process_t *current_process;
process_t *process_list;

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;
	irq_flags = process_irq_save();
	prev = NULL;
	for (scan = process_list; scan; scan = scan->next)
	{
		if (scan != target)
		{
			prev = scan;
			continue;
		}
		if (prev)
			prev->next = scan->next;
		else
			process_list = scan->next;
		scan->next = NULL;
		process_irq_restore(irq_flags);
		return 0;
	}
	process_irq_restore(irq_flags);
	return -ENOENT;
}
