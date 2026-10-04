/* SPDX-License-Identifier: GPL-3.0-only */
/* ARM64 debug-state lifecycle facade. */

#include <ir0/arch_debug.h>

void debug_state_init(void)
{
	/* No process-owned hardware breakpoint state is enabled during bring-up. */
}
