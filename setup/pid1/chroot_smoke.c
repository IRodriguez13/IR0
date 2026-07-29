/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * PID1 smoke: chroot(2) Linux-like root remap + fork inheritance.
 *
 * Tags (serial):
 *   CHROOT_OK / CHROOT_FAIL
 *   CHROOT_OPEN_INSIDE_OK
 *   CHROOT_OUTSIDE_DENIED
 *   CHROOT_GETCWD_OK
 *   CHROOT_FORK_INHERIT_OK
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static void tag(const char *s)
{
	write(1, s, strlen(s));
	write(1, "\n", 1);
}

static void fail(const char *why)
{
	tag("CHROOT_FAIL");
	tag(why);
	_exit(1);
}

static int write_file(const char *path, const char *data)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	ssize_t n;
	size_t len;

	if (fd < 0)
		return -1;
	len = strlen(data);
	n = write(fd, data, len);
	close(fd);
	return (n == (ssize_t)len) ? 0 : -1;
}

static int file_starts_with(const char *path, const char *expect)
{
	char buf[64];
	int fd = open(path, O_RDONLY);
	ssize_t n;

	if (fd < 0)
		return 0;
	n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0)
		return 0;
	buf[n] = '\0';
	return strncmp(buf, expect, strlen(expect)) == 0;
}

int main(void)
{
	char cwd[256];
	pid_t pid;
	int status = 0;
	int fd;

	if (mkdir("/jail", 0755) != 0 && errno != EEXIST)
		fail("mkdir_jail");
	if (write_file("/jail/inside.txt", "inside\n") != 0)
		fail("write_inside");
	if (write_file("/outside.txt", "outside\n") != 0)
		fail("write_outside");

	if (chroot("/jail") != 0)
		fail("chroot_call");

	if (!file_starts_with("/inside.txt", "inside"))
		fail("open_inside");
	tag("CHROOT_OPEN_INSIDE_OK");

	fd = open("/outside.txt", O_RDONLY);
	if (fd >= 0)
	{
		close(fd);
		fail("outside_visible");
	}
	tag("CHROOT_OUTSIDE_DENIED");

	if (chdir("/") != 0)
		fail("chdir_root");
	if (!getcwd(cwd, sizeof(cwd)))
		fail("getcwd");
	if (strcmp(cwd, "/") != 0)
		fail("getcwd_value");
	tag("CHROOT_GETCWD_OK");

	pid = fork();
	if (pid < 0)
		fail("fork");
	if (pid == 0)
	{
		if (!file_starts_with("/inside.txt", "inside"))
			_exit(11);
		fd = open("/outside.txt", O_RDONLY);
		if (fd >= 0)
		{
			close(fd);
			_exit(12);
		}
		_exit(0);
	}
	if (waitpid(pid, &status, 0) < 0)
		fail("wait");
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		fail("child");
	tag("CHROOT_FORK_INHERIT_OK");

	tag("CHROOT_OK");
	for (;;)
		pause();
	return 0;
}
