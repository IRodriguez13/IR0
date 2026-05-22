/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * PID1 smoke for userspace #PF -> SIGSEGV-equivalent exit.
 */

#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
	pid_t pid = fork();

	if (pid == 0)
	{
		char *argv[] = { "/bin/userspace_segv", 0 };

		execve("/bin/userspace_segv", argv, 0);
		_exit(127);
	}

	if (pid < 0)
	{
		static const char msg[] = "IR0: userspace_segv: fork failed\n";
		write(2, msg, sizeof(msg) - 1);
		return 1;
	}

	{
		(void)wait4(pid, 0, 0, 0);
		static const char ok[] = "IR0: userspace_segv smoke observed\n";
		write(1, ok, sizeof(ok) - 1);
		return 0;
	}

	return 4;
}
