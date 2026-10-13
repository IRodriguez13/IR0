/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only providers for the PMM bank-layout contract. */

#include <ir0/arch_mm.h>
#include <ir0/oops.h>
#include <ir0/process.h>

process_t *current_process;

size_t mm_boot_usable_range_count(void)
{
	return 0;
}

int mm_boot_usable_range(size_t index, struct ir0_phys_range *range)
{
	(void)index;
	(void)range;
	return -1;
}

pid_t process_get_pid(void)
{
	return -1;
}

process_t *process_find_by_pid(pid_t pid)
{
	(void)pid;
	return NULL;
}

void panicex(const char *message, panic_level_t level, const char *file,
	     int line, const char *caller)
{
	(void)message;
	(void)level;
	(void)file;
	(void)line;
	(void)caller;
	__builtin_trap();
}
