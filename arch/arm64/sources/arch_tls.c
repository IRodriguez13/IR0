/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 implementation of the semantic userspace TLS-base facade. */

#include <ir0/process.h>
#include <ir0/tls.h>

void tls_set_base(uint64_t base)
{
	__asm__ volatile("msr tpidr_el0, %0" :: "r"(base) : "memory");
}

uint64_t tls_get_base(void)
{
	uint64_t base;

	__asm__ volatile("mrs %0, tpidr_el0" : "=r"(base));
	return base;
}

void tls_restore_current(void)
{
	if (current_process)
		tls_set_base(process_tls_get(current_process));
}
