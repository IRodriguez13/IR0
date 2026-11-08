/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/* The handler's `ret` pops the restorer pushed just below the sigframe. */
#define IR0_ARCH_SIGFRAME_RETURN_SLOT 8UL
/*
 * Linux x86-64 has no kernel sigreturn trampoline: a handler installed
 * without SA_RESTORER cannot be delivered (setup_rt_frame -> SIGSEGV).
 */
#define IR0_ARCH_SIGTRAMP_HAVE 0
#define IR0_ARCH_SIGTRAMP_USER_BASE 0UL
#define IR0_ARCH_SIGTRAMP_CODE { 0 }
