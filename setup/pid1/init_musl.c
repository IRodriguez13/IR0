/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 musl static init smoke — validates libc startup + syscall insn ABI.
 *
 * Built by: make build-init-musl  (requires x86_64-linux-musl-gcc or musl-gcc)
 * Loaded by: sudo make load-init
 * Boot with: CONFIG_KERNEL_DEBUG_SHELL=n
 */

#include <unistd.h>

int main(void)
{
	static const char msg[] = "IR0: musl init smoke ok\n";

	write(1, msg, sizeof(msg) - 1);
	return 0;
}
