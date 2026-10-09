/* SPDX-License-Identifier: GPL-3.0-only */
/* Process-count policy shared by fork and its sysfs presentation. */

#include "process_internal.h"
#include <ir0/process_limits.h>

static uint32_t max_processes = 1024;

uint32_t process_limit_get(void)
{
	return max_processes;
}

int process_live_count(void)
{
	process_t *process;
	int count = 0;

	for (process = process_list; process; process = process->next)
		count++;
	return count;
}

int process_limit_set(uint32_t limit)
{
	if (limit < 1 || limit > 65535)
		return -EINVAL;
	if (limit < (uint32_t)process_live_count())
		return -EINVAL;
	max_processes = limit;
	return 0;
}
