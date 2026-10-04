/* SPDX-License-Identifier: GPL-3.0-only */
/* x86 debug-state lifecycle facade. */

#include <ir0/arch_debug.h>
#include <ir0/debug_trap.h>

void debug_state_init(void)
{
	ir0_debug_trap_init();
}
