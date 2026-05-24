/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal musl static harness — explicit arch_prctl(ARCH_SET_FS) gate for F.
 *
 * Uses musl CRT + write(2); arch_prctl and exit_group via inline syscall so
 * the harness matches init_musl.c linkage (sys/syscall.h breaks musl startup).
 *
 * Built: make build-musl-arch-prctl-smoke
 * Smoke: make smoke-musl-arch-prctl
 */

#include <unistd.h>

#define SYS_arch_prctl 158
#define SYS_exit_group 231
#define ARCH_SET_FS    0x1002

static char tls_area[128] __attribute__((aligned(64)));

static long ir0_syscall3(long nr, long a, long b, long c)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b), "d"(c)
		: "rcx", "r11", "memory");

	return ret;
}

static void halt_exit(int code)
{
	ir0_syscall3(SYS_exit_group, code, 0, 0);
	for (;;)
		__asm__ volatile("hlt");
}

int main(void)
{
	unsigned long fsbase = (unsigned long)(void *)tls_area;

	if (ir0_syscall3(SYS_arch_prctl, ARCH_SET_FS, (long)fsbase, 0) != 0)
		halt_exit(1);

	if (write(1, "ok\n", 3) != 3)
		halt_exit(2);

	if (write(1, "MUSL_ARCH_PRCTL_OK\n", 18) != 18)
		halt_exit(3);

	halt_exit(0);
	return 0;
}
