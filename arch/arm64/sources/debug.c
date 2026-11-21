/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 debug-state lifecycle provider. */

#include <asm/hw_breakpoint.h>
#include <ir0/oops.h>

void dump_registers(void)
{
}

void debug_state_init(void)
{
	/* No process-owned hardware breakpoint state is enabled during bring-up. */
}
