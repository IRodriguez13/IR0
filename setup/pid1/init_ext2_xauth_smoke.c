/* SPDX-License-Identifier: GPL-3.0-only */
/** Reproduce stock xauth locking against a persistent-style ext2 HOME. */

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void tag(const char *text)
{
	(void)write(STDOUT_FILENO, text, strlen(text));
}

int main(void)
{
	static char key[] = "00112233445566778899aabbccddeeff";
	pid_t child;
	int status;

	(void)mkdir("/home", 0755);
	if (mount("/dev/hdb", "/home", "ext2", 0, NULL) != 0)
	{
		perror("mount ext2 home");
		return 2;
	}
	(void)unlink("/home/ivan/.serverauth.ir0-smoke");
	(void)unlink("/home/ivan/.serverauth.ir0-smoke-c");
	(void)unlink("/home/ivan/.serverauth.ir0-smoke-l");
	child = fork();
	if (child == 0)
	{
		char *const argv[] = {
			"/usr/bin/xauth", "-f", "/home/ivan/.serverauth.ir0-smoke",
			"add", ":0", ".", key, NULL
		};

		if (setgid(100) != 0 || setuid(1000) != 0)
			_exit(126);
		execv(argv[0], argv);
		_exit(127);
	}
	if (child < 0 || waitpid(child, &status, 0) != child)
		return 3;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
	{
		tag("[XAUTH][FAIL] stock xauth lock/write\n");
		return 4;
	}
	if (access("/home/ivan/.serverauth.ir0-smoke", R_OK) != 0)
	{
		tag("[XAUTH][FAIL] authority file missing\n");
		return 5;
	}
	tag("[XAUTH] STOCK_XAUTH_EXT2_LOCK_OK\n[XAUTH_EXT2_OK]\n");
	for (;;)
		pause();
}
