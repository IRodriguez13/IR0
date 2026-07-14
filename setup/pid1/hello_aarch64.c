/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 — static aarch64 musl hello (toolchain smoke only; not IR0 guest init).
 */

#include <unistd.h>

int main(void)
{
	const char msg[] = "IR0_MUSL_AARCH64_HELLO_OK\n";

	if (write(1, msg, sizeof(msg) - 1) < 0)
		return 1;
	return 0;
}
