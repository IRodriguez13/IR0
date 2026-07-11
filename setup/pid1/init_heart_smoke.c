/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * HEART — /heart facade smoke (proc/sys mirror + kernel meta + src).
 */

#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void heart_fail(const char *step, const char *reason)
{
	write_str("[HEART][FAIL] step=");
	write_str(step ? step : "(null)");
	write_str(" reason=");
	write_str(reason ? reason : "(null)");
	write_str("\n");
	write_str("HEART_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int read_all(int fd, char *buf, size_t size)
{
	size_t used = 0;

	if (!buf || size == 0)
		return -1;

	for (;;)
	{
		ssize_t n;

		if (used >= size - 1)
			break;
		n = read(fd, buf + used, (size - 1) - used);
		if (n < 0)
			return -1;
		if (n == 0)
			break;
		used += (size_t)n;
	}
	buf[used] = '\0';
	return (int)used;
}

static int read_path(const char *path, char *buf, size_t size)
{
	int fd;
	int n;

	fd = open(path, O_RDONLY);
	if (fd < 0)
		return -1;
	n = read_all(fd, buf, size);
	close(fd);
	return n;
}

int main(void)
{
	char a[256];
	char b[256];
	char src[1024];
	struct stat st;
	int na;
	int nb;
	int fd;
	int dupfd;

	write_str("HEART_HARNESS_ID=heart-mvp-src\n");

	if (stat("/heart", &st) != 0 || !S_ISDIR(st.st_mode))
	{
		heart_fail("stat_heart", "not_dir");
		return 1;
	}
	if (stat("/proc", &st) != 0)
	{
		heart_fail("stat_proc_still", "missing");
		return 1;
	}
	if (stat("/sys", &st) != 0)
	{
		heart_fail("stat_sys_still", "missing");
		return 1;
	}
	write_str("HEART_ROOT_OK\n");

	na = read_path("/proc/uptime", a, sizeof(a));
	nb = read_path("/heart/proc/uptime", b, sizeof(b));
	if (na < 1 || nb < 1 || strcmp(a, b) != 0)
	{
		heart_fail("mirror_uptime", "mismatch");
		return 1;
	}
	write_str("HEART_PROC_MIRROR_OK\n");

	na = read_path("/sys/kernel/version", a, sizeof(a));
	nb = read_path("/heart/sys/kernel/version", b, sizeof(b));
	if (na < 1 || nb < 1 || strcmp(a, b) != 0)
	{
		heart_fail("mirror_sys_version", "mismatch");
		return 1;
	}
	write_str("HEART_SYS_MIRROR_OK\n");

	nb = read_path("/heart/kernel/version", b, sizeof(b));
	if (nb < 1 || b[0] == '\0')
	{
		heart_fail("kernel_version", "empty");
		return 1;
	}
	write_str("HEART_KERNEL_META_OK\n");

	nb = read_path("/heart/README", b, sizeof(b));
	if (nb < 8 || strstr(b, "IR0") == NULL)
	{
		heart_fail("readme", "bad");
		return 1;
	}
	write_str("HEART_README_OK\n");

	fd = open("/heart/src/includes/ir0/heart_src_probe.h", O_RDONLY);
	if (fd < 0)
	{
		heart_fail("heart_src_open", "open");
		return 1;
	}
	nb = read_all(fd, src, sizeof(src));
	close(fd);
	if (nb < 16 || strstr(src, "HEART_SRC_MARKER_V1") == NULL)
	{
		heart_fail("heart_src", "missing_marker");
		return 1;
	}
	write_str("HEART_SRC_OK\n");

	fd = open("/heart/src/includes/ir0/sock_stream.h", O_RDONLY);
	if (fd < 0)
	{
		heart_fail("heart_src_sock_stream", "open");
		return 1;
	}
	nb = read_all(fd, src, sizeof(src));
	close(fd);
	if (nb < 16 || strstr(src, "sock_stream") == NULL)
	{
		heart_fail("heart_src_sock_stream", "content");
		return 1;
	}
	write_str("HEART_SRC_EXPAND_OK\n");

	na = read_path("/proc/cmdline", a, sizeof(a));
	nb = read_path("/heart/proc/cmdline", b, sizeof(b));
	if (na < 1 || nb < 1 || strcmp(a, b) != 0 || strstr(a, "console=") == NULL)
	{
		heart_fail("mirror_cmdline", "mismatch");
		return 1;
	}
	write_str("HEART_PROC_CMDLINE_OK\n");

	na = read_path("/sys/kernel/osrelease", a, sizeof(a));
	nb = read_path("/heart/sys/kernel/osrelease", b, sizeof(b));
	if (na < 1 || nb < 1 || strcmp(a, b) != 0)
	{
		heart_fail("mirror_osrelease", "mismatch");
		return 1;
	}
	write_str("HEART_SYS_OSRELEASE_OK\n");

	fd = open("/heart/proc/uptime", O_RDONLY);
	if (fd < 0)
	{
		heart_fail("open_heart_uptime", "open");
		return 1;
	}
	dupfd = dup(fd);
	if (dupfd < 0)
	{
		heart_fail("dup_heart_uptime", "dup");
		close(fd);
		return 1;
	}
	close(dupfd);
	close(fd);
	write_str("HEART_DUP_OK\n");

	write_str("HEART_OK\n");
	return 0;
}
