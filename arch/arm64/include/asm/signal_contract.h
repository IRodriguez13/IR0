/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
/*
 * AAPCS64 handlers return through x30, so no return slot sits below the
 * sigframe and the handler SP stays 16-byte aligned.
 */
#define IR0_ARCH_SIGFRAME_RETURN_SLOT 0UL
/*
 * Linux arm64 returns handlers installed without SA_RESTORER through the
 * vDSO __kernel_rt_sigreturn. IR0 has no ARM64 vDSO yet, so the same two
 * instructions live in a kernel sigpage mapped at exec, just below the
 * x86-64 vDSO slot: mov x8, #__NR_rt_sigreturn (139); svc #0.
 */
#define IR0_ARCH_SIGTRAMP_HAVE 1
#define IR0_ARCH_SIGTRAMP_USER_BASE 0x7fcff000UL
#define IR0_ARCH_SIGTRAMP_CODE \
	{ 0x68, 0x11, 0x80, 0xd2, 0x01, 0x00, 0x00, 0xd4 }
