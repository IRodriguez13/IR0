/**
 * IR0 — kill(2) SIGTERM / existence-probe smoke (no #UD on teardown)
 */
/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <sys/wait.h>
#include <unistd.h>

static int tag_ok(const char *t)
{
	printf("%s\n", t);
	fflush(stdout);
	return 0;
}

static int fail(const char *t, int err)
{
	printf("%s errno=%d\n", t, err);
	fflush(stdout);
	return 1;
}

int main(void)
{
	pid_t pid;
	int status = -1;
	int kret;

	pid = fork();
	if (pid < 0)
		return fail("KILL_SIGTERM_FORK_FAIL", errno);
	if (pid == 0)
	{
		for (;;)
			pause();
	}

	/* Existence probe must not wake/kill. */
	kret = kill(pid, 0);
	if (kret != 0)
		return fail("KILL_PROBE0_FAIL", errno);
	tag_ok("KILL_PROBE0_OK");

	kret = kill(pid, SIGTERM);
	if (kret != 0)
		return fail("KILL_SIGTERM_FAIL", errno);
	tag_ok("KILL_SIGTERM_SENT_OK");

	if (waitpid(pid, &status, 0) != pid)
		return fail("KILL_SIGTERM_WAIT_FAIL", errno);
	if (!WIFSIGNALED(status) || WTERMSIG(status) != SIGTERM)
		return fail("KILL_SIGTERM_STATUS_FAIL", status);

	tag_ok("KILL_SIGTERM_OK");
	return 0;
}
