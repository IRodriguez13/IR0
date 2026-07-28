/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * PID1 smoke for userspace #PF -> WIFSIGNALED(SIGSEGV) wait status.
 * Linux ash prints "Segmentation fault" only when exit_signal is set
 * (not exited(139)).
 */

#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>

int main(void)
{
	pid_t pid = fork();
	int status = 0;

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

	if (wait4(pid, &status, 0, 0) < 0)
	{
		static const char msg[] = "IR0: userspace_segv: wait4 failed\n";
		write(2, msg, sizeof(msg) - 1);
		return 2;
	}

	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGSEGV)
	{
		static const char msg[] =
			"IR0: userspace_segv: expected WIFSIGNALED(SIGSEGV)\n";
		write(2, msg, sizeof(msg) - 1);
		return 3;
	}

	{
		static const char ok[] = "IR0: userspace_segv smoke observed\n";
		write(1, ok, sizeof(ok) - 1);
		return 0;
	}
}
