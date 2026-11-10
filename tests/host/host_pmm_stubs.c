/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only providers for the PMM bank-layout contract. */

#include <ir0/arch_mm.h>
#include <ir0/boot_memory.h>
#include <ir0/boot_params.h>
#include <ir0/oops.h>
#include <ir0/process.h>

process_t *current_process;

int ir0_boot_port_snapshot_fill(const struct ir0_boot_params *params,
				struct ir0_boot_snapshot *snapshot)
{
	if (!params || params->info != 1 || !snapshot)
		return -1;
	snapshot->usable[0].base = 0x10000U;
	snapshot->usable[0].size = 0x8000U;
	snapshot->usable_count = 1;
	snapshot->reserved[0].base = 0x12000U;
	snapshot->reserved[0].size = 0x2000U;
	snapshot->reserved_count = 1;
	snapshot->cmdline[0] = 'i';
	snapshot->cmdline[1] = 'r';
	snapshot->cmdline[2] = '0';
	snapshot->cmdline[3] = '\0';
	snapshot->valid = 1;
	return 0;
}

/* No heap window on the host: boot_memory protects only the snapshot. */
uintptr_t mm_kernel_heap_start(void)
{
	return 0;
}

size_t mm_kernel_heap_size(void)
{
	return 0;
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
