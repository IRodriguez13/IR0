/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: init_tcc_power_halt.c
 * Description: Harness — TCC in guest compiles power_halt.c, then execs it (KTM + reboot HALT).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define TCC_BIN "/bin/tcc"
#define TCC_LIB "/lib/tcc"
#define SRC_PATH "/tmp/ph.c"
#define BIN_PATH "/tmp/ph"

/*
 * Guest program compiled by TinyCC in ring 3.
 * Raw syscalls (fase52 libc.a is stdio-min; no write/open/syscall).
 * Optional /dev/ktm via inline uapi, then reboot(HALT).
 */
static const char GUEST_SRC[] =
	"#define NR_write  1\n"
	"#define NR_open   2\n"
	"#define NR_close  3\n"
	"#define NR_ioctl  16\n"
	"#define NR_reboot 169\n"
	"#define O_RDWR 2\n"
	"\n"
	"#define LINUX_REBOOT_MAGIC1 0xfee1dead\n"
	"#define LINUX_REBOOT_MAGIC2 672274793\n"
	"#define LINUX_REBOOT_CMD_HALT 0xCDEF0123u\n"
	"\n"
	"#define KTM_IOC_TAKE_SNAPSHOT  0x4B03u\n"
	"#define KTM_IOC_USER_EVENT     0x4B07u\n"
	"#define KTM_UAPI_EVENT_CASE_BEGIN 21u\n"
	"#define KTM_UAPI_EVENT_CASE_END   22u\n"
	"#define KTM_UAPI_SUBSYS_TEST      7u\n"
	"\n"
	"typedef struct {\n"
	"\tunsigned type;\n"
	"\tunsigned subsystem;\n"
	"\tunsigned long long arg0, arg1, arg2, arg3;\n"
	"\tchar name[64];\n"
	"} ktm_user_event_t;\n"
	"\n"
	"typedef struct {\n"
	"\tunsigned scope, flags;\n"
	"\tunsigned long long total_frames, used_frames, free_frames;\n"
	"\tunsigned long long processes, zombies, open_fds, pipes;\n"
	"} ktm_ioc_snapshot_t;\n"
	"\n"
	"static long raw_syscall(long n, long a, long b, long c, long d)\n"
	"{\n"
	"\tlong ret;\n"
	"\tregister long r10 __asm__(\"r10\") = d;\n"
	"\t__asm__ __volatile__(\"syscall\"\n"
	"\t\t: \"=a\"(ret)\n"
	"\t\t: \"a\"(n), \"D\"(a), \"S\"(b), \"d\"(c), \"r\"(r10)\n"
	"\t\t: \"rcx\", \"r11\", \"memory\");\n"
	"\treturn ret;\n"
	"}\n"
	"\n"
	"static void say(const char *s)\n"
	"{\n"
	"\tconst char *p = s;\n"
	"\twhile (*p) p++;\n"
	"\t(void)raw_syscall(NR_write, 1, (long)s, (long)(p - s), 0);\n"
	"}\n"
	"\n"
	"static void zmem(void *p, unsigned n)\n"
	"{\n"
	"\tunsigned char *b = (unsigned char *)p;\n"
	"\twhile (n--) *b++ = 0;\n"
	"}\n"
	"\n"
	"static int ktm_user_event(int fd, unsigned type, const char *name)\n"
	"{\n"
	"\tktm_user_event_t ev;\n"
	"\tzmem(&ev, sizeof(ev));\n"
	"\tev.type = type;\n"
	"\tev.subsystem = KTM_UAPI_SUBSYS_TEST;\n"
	"\tif (name) {\n"
	"\t\tunsigned i;\n"
	"\t\tfor (i = 0; i < 63 && name[i]; i++)\n"
	"\t\t\tev.name[i] = name[i];\n"
	"\t\tev.name[i] = 0;\n"
	"\t}\n"
	"\treturn (int)raw_syscall(NR_ioctl, fd, (long)KTM_IOC_USER_EVENT, (long)&ev, 0);\n"
	"}\n"
	"\n"
	"int main(void)\n"
	"{\n"
	"\tint fd;\n"
	"\tktm_ioc_snapshot_t snap;\n"
	"\n"
	"\tsay(\"POWER_TCC_COMPILE_OK\\n\");\n"
	"\n"
	"\tfd = (int)raw_syscall(NR_open, (long)\"/dev/ktm\", O_RDWR, 0, 0);\n"
	"\tif (fd < 0) {\n"
	"\t\tsay(\"POWER_TCC_KTM_SKIP\\n\");\n"
	"\t} else {\n"
	"\t\tint ok = 1;\n"
	"\t\tif (ktm_user_event(fd, KTM_UAPI_EVENT_CASE_BEGIN, \"tcc_power_halt\") != 0)\n"
	"\t\t\tok = 0;\n"
	"\t\tzmem(&snap, sizeof(snap));\n"
	"\t\tif (raw_syscall(NR_ioctl, fd, (long)KTM_IOC_TAKE_SNAPSHOT, (long)&snap, 0) != 0)\n"
	"\t\t\tok = 0;\n"
	"\t\telse if (snap.total_frames == 0)\n"
	"\t\t\tok = 0;\n"
	"\t\tif (ktm_user_event(fd, KTM_UAPI_EVENT_CASE_END, \"tcc_power_halt\") != 0)\n"
	"\t\t\tok = 0;\n"
	"\t\t(void)raw_syscall(NR_close, fd, 0, 0, 0);\n"
	"\t\tsay(ok ? \"POWER_TCC_KTM_OK\\n\" : \"POWER_TCC_KTM_FAIL\\n\");\n"
	"\t\tif (ok)\n"
	"\t\t\tsay(\"KTM_USERDEV_OK\\n\");\n"
	"\t}\n"
	"\n"
	"\tsay(\"POWER_TCC_HALT_CALL\\n\");\n"
	"\t(void)raw_syscall(NR_reboot, LINUX_REBOOT_MAGIC1, LINUX_REBOOT_MAGIC2,\n"
	"\t\t\t (long)LINUX_REBOOT_CMD_HALT, 0);\n"
	"\tsay(\"POWER_TCC_HALT_RETURNED\\n\");\n"
	"\treturn 1;\n"
	"}\n";

static void write_str(const char *s)
{
	const char *p = s;

	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static int write_source(const char *path, const char *src)
{
	int fd;
	size_t len;

	len = strlen(src);
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0)
		return -1;
	if (write(fd, src, len) != (ssize_t)len)
	{
		close(fd);
		return -1;
	}
	close(fd);
	return 0;
}

static int run_wait(char *const argv[], int *exit_code)
{
	pid_t pid;
	int status;
	int errfd;

	errfd = open("/tmp/tcc.err", O_WRONLY | O_CREAT | O_TRUNC, 0644);
	pid = fork();
	if (pid < 0)
	{
		if (errfd >= 0)
			close(errfd);
		return -1;
	}
	if (pid == 0)
	{
		if (errfd >= 0)
		{
			dup2(errfd, 2);
			close(errfd);
		}
		execv(argv[0], argv);
		_exit(127);
	}
	if (errfd >= 0)
		close(errfd);
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (WIFEXITED(status))
		*exit_code = WEXITSTATUS(status);
	else
		*exit_code = 128;
	return 0;
}

static void dump_tcc_err(void)
{
	char buf[512];
	int fd;
	ssize_t n;

	fd = open("/tmp/tcc.err", O_RDONLY);
	if (fd < 0)
		return;
	write_str("[TCC_POWER][LINK_ERR] ");
	while ((n = read(fd, buf, sizeof(buf))) > 0)
		(void)write(1, buf, (size_t)n);
	close(fd);
	write_str("\n");
}

int main(void)
{
	char *link_argv[] = {
		(char *)TCC_BIN,
		(char *)"-B",
		(char *)TCC_LIB,
		(char *)"-static",
		(char *)"-o",
		(char *)BIN_PATH,
		(char *)SRC_PATH,
		NULL
	};
	char *run_argv[] = { (char *)BIN_PATH, NULL };
	int ec = -1;

	write_str("TCC_POWER_HARNESS_ID=init_tcc_power_halt.c\n");

	if (write_source(SRC_PATH, GUEST_SRC) != 0)
	{
		write_str("TCC_POWER_FAIL write_src\n");
		return 1;
	}

	if (run_wait(link_argv, &ec) != 0 || ec != 0)
	{
		write_str("TCC_POWER_FAIL tcc_link ec=");
		{
			char b[16];
			int n = 0;
			unsigned u = (unsigned)(ec < 0 ? 0 : ec);
			char tmp[16];
			int i = 0;

			if (u == 0)
				b[n++] = '0';
			else
			{
				while (u)
				{
					tmp[i++] = (char)('0' + (u % 10));
					u /= 10;
				}
				while (i--)
					b[n++] = tmp[i];
			}
			b[n++] = '\n';
			(void)write(1, b, (size_t)n);
		}
		dump_tcc_err();
		return 1;
	}

	write_str("TCC_POWER_LINK_OK\n");

	execv(BIN_PATH, run_argv);
	write_str("TCC_POWER_FAIL exec\n");
	return 1;
}
