/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 minimal PID 1 — musl static, fork + execve + wait4 reap loop.
 *
 * Root is already mounted by the kernel (vfs_init_root). No mount / here.
 *
 * Built by: make build-init-minimal
 * Loaded by: make load-init (or load-init-with-musl after swapping binary)
 * Boot with: CONFIG_KERNEL_DEBUG_SHELL=n
 *
 * Oleada 2: requires /bin/sh on disk.img for interactive smoke.
 */

#include <unistd.h>
#include <sys/wait.h>
#include <string.h>

int main(void)
{
	pid_t pid;
	static const char no_sh[] = "IR0: init_minimal: /bin/sh missing\n";

	pid = fork();
	if (pid == 0)
	{
		char *argv[] = { "/bin/sh", NULL };

		execve("/bin/sh", argv, NULL);
		write(2, no_sh, sizeof(no_sh) - 1);
		_exit(127);
	}

	if (pid < 0)
	{
		static const char msg[] = "IR0: init_minimal: fork failed\n";

		write(2, msg, sizeof(msg) - 1);
		return 1;
	}

	for (;;)
	{
		int status;

		if (wait4(-1, &status, 0, NULL) < 0)
			continue;
	}

	return 0;
}
