/* SPDX-License-Identifier: GPL-3.0-only */
/* x86 debug-state lifecycle provider. */

#include <asm/hw_breakpoint.h>
#include <asm/debug_trap.h>

void debug_state_init(void)
{
	ir0_debug_trap_init();
}
