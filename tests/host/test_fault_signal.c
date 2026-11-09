/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * fault_signal() follows Linux for every synchronous fault class an ISA
 * reports outside the MM abort path (arch/arm64/kernel/traps.c and
 * fpsimd.c do_fpsimd_exc(), arch/x86/kernel/traps.c). Expected values
 * come from the host <signal.h>, i.e. Linux itself.
 */

#include "test_harness.h"

#include <signal.h>
#include <ir0/fault.h>

static int signal_for(enum ir0_fault_kind kind)
{
	struct ir0_fault fault = { 0 };

	fault.kind = kind;
	return fault_signal(&fault);
}

void test_fault_signal_linux_classes(void)
{
	TEST_BEGIN("fault_signal maps fault classes like Linux");
	ASSERT_EQ(signal_for(IR0_FAULT_ILLEGAL_INSN), SIGILL);
	ASSERT_EQ(signal_for(IR0_FAULT_BREAKPOINT), SIGTRAP);
	ASSERT_EQ(signal_for(IR0_FAULT_ALIGNMENT), SIGBUS);
	/* ARM64 ESR EC FP_EXC64 and x86 #DE/#MF/#XM are SIGFPE. */
	ASSERT_EQ(signal_for(IR0_FAULT_ARITHMETIC), SIGFPE);
	TEST_END();
}

/*
 * The x86 segment and protection classes (arch/x86/kernel/traps.c):
 * #GP is SIGSEGV, #SS and #NP are SIGBUS (do_error_trap(..., SIGBUS)), a page
 * fault that reaches the signal path is SIGSEGV. #DF never becomes a user
 * signal: 0 tells the caller the fault is fatal to the kernel.
 */
void test_fault_signal_x86_classes(void)
{
	TEST_BEGIN("fault_signal maps x86 segment classes like Linux");
	ASSERT_EQ(signal_for(IR0_FAULT_PROTECTION), SIGSEGV);
	ASSERT_EQ(signal_for(IR0_FAULT_STACK_SEGMENT), SIGBUS);
	ASSERT_EQ(signal_for(IR0_FAULT_SEGMENT_NOT_PRESENT), SIGBUS);
	ASSERT_EQ(signal_for(IR0_FAULT_PAGE), SIGSEGV);
	ASSERT_EQ(signal_for(IR0_FAULT_DOUBLE), 0);
	TEST_END();
}
