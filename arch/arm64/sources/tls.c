/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 userspace TLS-base provider. */

#include <ir0/process.h>
#include <ir0/tls.h>

void tls_set_base(uint64_t base)
{
	__asm__ volatile("msr tpidr_el0, %0" :: "r"(base) : "memory");
}

void tls_restore_current(void)
{
	if (current_process)
		tls_set_base(process_tls_get(current_process));
}
