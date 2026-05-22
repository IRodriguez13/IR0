/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Minimal /bin/sh stub for oleada 2 smoke — musl static, ring-3 execve target.
 *
 * Built by: make build-sh-smoke
 * Injected as: /bin/sh on disk.img (see load-userspace-rootfs)
 */

#include <unistd.h>
#include <sys/syscall.h>

int main(void)
{
	pid_t pid = getpid();
	long tid = syscall(SYS_gettid);
	static const char msg[] = "IR0: shell smoke ok\n";
	static const char bad[] = "IR0: shell smoke syscall check failed\n";

	if (pid <= 0 || tid <= 0)
	{
		write(2, bad, sizeof(bad) - 1);
		return 2;
	}

	write(1, msg, sizeof(msg) - 1);
	return 0;
}
