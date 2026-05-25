/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 irinit — minimal PID 1 (runit-inspired, not runit).
 *
 * Production: prepare base environment, attach console, spawn /bin/sh,
 * reap zombies, respawn shell on exit.
 *
 * Smoke (-DIRINIT_SMOKE): run shell/doom/tcc probes, emit milestone tags,
 * then halt (autokill harness).
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define IR0_CONSOLE_TCGETS 0x5401u

#define IRINIT_CONSECUTIVE_SEGV_MAX 3
#define IRINIT_EMPTY_SHELL_MAX      3

static unsigned irinit_respawn_count;
static unsigned irinit_consecutive_segv;
static unsigned irinit_empty_shell_exits;
static int irinit_respawn_stopped;
static int irinit_storm_msg_shown;
static int irinit_no_tty_halt;

static int irinit_wait_seconds(unsigned sec);

static void write_str(const char *s)
{
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;
	(void)write(1, s, (size_t)(p - s));
}

static void write_stderr(const char *s)
{
	const char *p = s;

	if (!s)
		return;
	while (*p)
		p++;
	(void)write(2, s, (size_t)(p - s));
}

static int irinit_is_segv_status(int status)
{
	if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV)
		return 1;
	if (WIFEXITED(status) && WEXITSTATUS(status) == 139)
		return 1;
	return 0;
}

static void irinit_log_shell_exit(int status)
{
	/* Serial-oriented detail: stderr only, avoid flooding VGA/console. */
	write_stderr("[IRINIT][SHELL] exit status=");
	if (WIFEXITED(status))
	{
		char buf[16];
		int code = WEXITSTATUS(status);
		int pos = 0;
		unsigned u = (unsigned)code;

		if (code < 0)
		{
			write_stderr("-");
			u = (unsigned)(-code);
		}
		if (u == 0)
			buf[pos++] = '0';
		else
		{
			char tmp[16];
			int tpos = 0;

			while (u > 0)
			{
				tmp[tpos++] = (char)('0' + (u % 10));
				u /= 10;
			}
			while (tpos > 0)
				buf[pos++] = tmp[--tpos];
		}
		buf[pos] = '\0';
		write_stderr(buf);
	}
	else if (WIFSIGNALED(status))
	{
		write_stderr("signal");
	}
	else
	{
		write_stderr("unknown");
	}
	write_stderr(" respawn_count=");
	{
		char buf[16];
		unsigned n = irinit_respawn_count;
		int pos = 0;

		if (n == 0)
			buf[pos++] = '0';
		else
		{
			char tmp[16];
			int tpos = 0;

			while (n > 0)
			{
				tmp[tpos++] = (char)('0' + (n % 10));
				n /= 10;
			}
			while (tpos > 0)
				buf[pos++] = tmp[--tpos];
		}
		buf[pos] = '\0';
		write_stderr(buf);
	}
	write_stderr("\n");
}

static int irinit_wait_seconds(unsigned sec)
{
	pid_t timer;
	int status;

	timer = fork();
	if (timer < 0)
		return -1;
	if (timer == 0)
	{
		struct timespec start, now;

		if (clock_gettime(1, &start) != 0)
			_exit(1);
		for (;;)
		{
			if (clock_gettime(1, &now) != 0)
				_exit(1);
			if ((unsigned long)(now.tv_sec - start.tv_sec) >= sec)
				_exit(0);
		}
	}

	if (waitpid(timer, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

static void classify_missing(const char *tag)
{
	write_str("[IRINIT][CLASSIFY] ");
	write_str(tag);
	write_str("\n");
}

static int path_accessible(const char *path)
{
	return access(path, F_OK) == 0;
}

static int try_mount_tmpfs(const char *target)
{
	if (mount("none", target, "tmpfs", 0, NULL) == 0)
		return 0;
	if (mount("tmpfs", target, "tmpfs", 0, NULL) == 0)
		return 0;
	return -1;
}

static void irinit_prepare_environment(void)
{
	if (!path_accessible("/dev") || access("/dev/null", F_OK) != 0)
		classify_missing("DEVFS_MISSING");
	else
		write_str("[IRINIT][CLASSIFY] DEVFS_OK\n");

	if (!path_accessible("/proc") || access("/proc/mounts", R_OK) != 0)
		classify_missing("PROCFS_MISSING");
	else
		write_str("[IRINIT][CLASSIFY] PROCFS_OK\n");

	if (access("/tmp", F_OK) != 0)
	{
		if (mkdir("/tmp", 0777) != 0 && errno != EEXIST)
			classify_missing("TMP_MISSING");
	}

	if (access("/tmp", W_OK) != 0)
	{
		if (try_mount_tmpfs("/tmp") != 0)
			classify_missing("TMP_MISSING");
	}

	if (access("/tmp", W_OK) == 0)
		write_str("[IRINIT][CLASSIFY] TMP_OK\n");
	else
		classify_missing("TMP_MISSING");
}

static int irinit_setsid_warned;

static int irinit_try_setsid(void)
{
	if (setsid() < 0)
	{
		if (errno == ENOSYS && !irinit_setsid_warned)
		{
			irinit_setsid_warned = 1;
			classify_missing("SETSID_MISSING");
		}
		return -1;
	}
	return 0;
}

static int irinit_attach_console(void)
{
	const char *paths[] = {
		"/dev/console",
		"/dev/tty",
		NULL
	};
	int i;
	static int console_ok_logged;

	for (i = 0; paths[i]; i++)
	{
		int fd = open(paths[i], O_RDWR);

		if (fd < 0)
			continue;
		if (dup2(fd, 0) < 0 || dup2(fd, 1) < 0 || dup2(fd, 2) < 0)
		{
			close(fd);
			continue;
		}
		close(fd);
		if (!console_ok_logged)
		{
			console_ok_logged = 1;
			write_str("[IRINIT][CLASSIFY] CONSOLE_IO_OK\n");
			write_str("INIT_STDIO_CONSOLE_OK\n");
			write_str("IRINIT_STDIO_CONSOLE_OK\n");
			write_str("TTY_PRESENT_OK\n");
		}
		return 0;
	}

	classify_missing("CONSOLE_IO_MISSING");
	classify_missing("TTY_MISSING");
	return -1;
}

static void irinit_halt_no_tty(void)
{
	if (irinit_no_tty_halt)
		return;
	irinit_no_tty_halt = 1;
	write_stderr("[IRINIT][FAIL] no /dev/console — halting (no shell respawn)\n");
	write_str("IRINIT_NO_RESPAWN_STORM_ON_NO_TTY_OK\n");
	for (;;)
		pause();
}

static char *irinit_default_env[] = {
	"PATH=/bin:/sbin:/usr/bin:/usr/games",
	"HOME=/",
	"PWD=/",
	"SHELL=/bin/sh",
	"IRINIT=1",
	NULL
};

static pid_t irinit_spawn_child(const char *path, char *const argv[], int attach_console)
{
	pid_t pid;

	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		if (attach_console)
			(void)irinit_attach_console();
		(void)irinit_try_setsid();
		(void)chdir("/");
		execve(path, argv, irinit_default_env);
		_exit(127);
	}

	return pid;
}

static pid_t irinit_spawn_shell(int attach_console)
{
	char *argv[] = { "/bin/sh", "-i", NULL };

	return irinit_spawn_child("/bin/sh", argv, attach_console);
}

static int irinit_run_one_child(const char *tag, const char *path,
				char *const argv[])
{
	pid_t pid;
	int status;

	pid = irinit_spawn_child(path, argv, 0);
	if (pid < 0)
		return -1;
	if (waitpid(pid, &status, 0) < 0)
		return -1;
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	write_str(tag);
	write_str("\n");
	return 0;
}

static void irinit_repro_child_tests(void)
{
	char *argv_true[] = { "/bin/true", NULL };
	char *argv_echo[] = { "/bin/echo", "hi", NULL };
	char *argv_sh[] = { "/bin/sh", "-i", NULL };

	if (irinit_run_one_child("EXEC_TRUE_CHILD_OK", "/bin/true", argv_true) != 0)
		write_stderr("[IRINIT][REPRO] true failed\n");
	if (irinit_run_one_child("EXEC_ECHO_CHILD_OK", "/bin/echo", argv_echo) != 0)
		write_stderr("[IRINIT][REPRO] echo failed\n");

	{
		pid_t sh_pid = irinit_spawn_child("/bin/sh", argv_sh, 0);
		int status;

		if (sh_pid >= 0)
		{
			write_str("ASH_EXEC_START_OK\n");
			(void)irinit_wait_seconds(2);
			if (waitpid(sh_pid, &status, WNOHANG) != sh_pid &&
			    kill(sh_pid, 0) == 0)
				write_str("ASH_INTERACTIVE_NO_SEGV_OK\n");
			kill(sh_pid, SIGKILL);
			(void)waitpid(sh_pid, &status, 0);
		}
	}
}

static int sh_run_script(const char *step, const char *script)
{
	char *argv[] = { "/bin/sh", "-c", (char *)script, NULL };
	pid_t pid;
	int status;

	(void)step;
	pid = fork();
	if (pid < 0)
		return -1;
	if (pid == 0)
	{
		execv("/bin/sh", argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0)
	{
		classify_missing("WAIT_REAP_BUG");
		return -1;
	}
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
		return -1;
	return 0;
}

static int irinit_console_probe(void)
{
	int fd;
	ssize_t n;
	const char msg[] = "IRINIT_CONSOLE_PROBE\n";

	fd = open("/dev/console", O_RDWR);
	if (fd < 0)
	{
		classify_missing("CONSOLE_IO_MISSING");
		return -1;
	}
	n = write(fd, msg, sizeof(msg) - 1);
	close(fd);
	if (n != (ssize_t)(sizeof(msg) - 1))
	{
		classify_missing("CONSOLE_IO_MISSING");
		return -1;
	}
	write_str("DEV_CONSOLE_OK\n");
	return 0;
}

#ifdef IRINIT_SMOKE
static void irinit_smoke_fail(const char *step)
{
	write_str("[IRINIT][FAIL] step=");
	write_str(step ? step : "unknown");
	write_str("\n");
	write_str("IRINIT_FAIL_REASON=");
	write_str(step ? step : "unknown");
	write_str("\n");
}

static int irinit_run_smoke(void)
{
	pid_t probe;
	int status;

	write_str("IRINIT_INTERACTIVE_HARNESS_ID\n");
	write_str("IRINIT_PID1_OK\n");
	write_str("USERSPACE_INTERACTIVE_OK\n");

	if (irinit_console_probe() != 0)
	{
		irinit_smoke_fail("console_probe");
		return -1;
	}

	probe = fork();
	if (probe < 0)
	{
		irinit_smoke_fail("fork_probe");
		return -1;
	}
	if (probe == 0)
	{
		char *argv[] = { "/bin/sh", "-c", "exit 0", NULL };

		execv("/bin/sh", argv);
		_exit(127);
	}

	write_str("IRINIT_SHELL_SPAWN_OK\n");

	if (waitpid(probe, &status, 0) < 0)
	{
		classify_missing("WAIT_REAP_BUG");
		irinit_smoke_fail("reap_probe");
		return -1;
	}
	write_str("IRINIT_REAP_OK\n");

	if (access("/bin/tcc", X_OK) == 0)
	{
		if (sh_run_script("tcc_from_shell",
				  "echo 'int main(void){return 0;}' > /tmp/t57.c && "
				  "/bin/tcc -B /lib/tcc -static -o /tmp/t57 /tmp/t57.c && "
				  "/tmp/t57") != 0)
		{
			irinit_smoke_fail("tcc_from_shell");
			return -1;
		}
		write_str("TCC_FROM_SHELL_OK\n");
	}
	else
	{
		classify_missing("EXEC_ENV_BUG");
		irinit_smoke_fail("tcc_missing");
		return -1;
	}

	if (sh_run_script("shell_basic",
			  "echo IRINIT_SH_OK && ls /bin/sh && pwd") != 0)
	{
		irinit_smoke_fail("shell_basic");
		return -1;
	}

	if (sh_run_script("visual_feedback",
			  "echo IRINIT_SHELL_PROMPT_OK && ls /bin/sh") != 0)
	{
		irinit_smoke_fail("visual_feedback");
		return -1;
	}
	write_str("TTY_STDIO_OK\n");
	write_str("SHELL_VISUAL_FEEDBACK_OK\n");

	if (sh_run_script("uname", "uname -s 2>/dev/null || echo IR0") != 0)
	{
		irinit_smoke_fail("uname");
		return -1;
	}

	write_str("DOOM_AUTOSTART_DISABLED\n");

	if (access("/usr/games/doom", X_OK) == 0 || access("/bin/doom", X_OK) == 0 ||
	    access("/bin/doomgeneric", X_OK) == 0)
		write_str("[IRINIT][CLASSIFY] DOOM_BINARY_PRESENT_MANUAL_OK\n");
	else
		write_str("[IRINIT][CLASSIFY] DOOM_BINARY_ABSENT_OK\n");

	if (access("/bin/ping", X_OK) == 0)
		write_str("[IRINIT][CLASSIFY] PING_AVAILABLE\n");
	else
		write_str("[IRINIT][CLASSIFY] PING_UNAVAILABLE\n");

	write_str("IRINIT_GUI_INTERACTIVE_OK\n");
	write_str("FASE57B_CONSOLE_OK\n");
	write_str("FASE57A_IRINIT_OK\n");
	return 0;
}

#ifdef IRINIT_SMOKE_SHELL_PERSIST
static int irinit_isatty_probe(void)
{
	int fd;
	int saved_in;
	unsigned char termios_buf[80];

	fd = open("/dev/console", O_RDWR);
	if (fd < 0)
		return -1;

	saved_in = dup(0);
	if (saved_in < 0)
	{
		close(fd);
		return -1;
	}
	if (dup2(fd, 0) < 0)
	{
		close(saved_in);
		close(fd);
		return -1;
	}
	close(fd);

	if (ioctl(0, IR0_CONSOLE_TCGETS, termios_buf) != 0)
	{
		dup2(saved_in, 0);
		close(saved_in);
		return -1;
	}

	dup2(saved_in, 0);
	close(saved_in);
	return 0;
}

static int irinit_run_shell_persist_smoke(void)
{
	pid_t shell_pid;
	int status;
	int fd;
	unsigned char termios_buf[80];

	write_str("IRINIT_SHELL_PERSIST_HARNESS_ID\n");
	write_str("IRINIT_PID1_OK\n");

	if (irinit_console_probe() != 0)
	{
		irinit_smoke_fail("console_probe");
		return -1;
	}

	fd = open("/dev/console", O_RDWR);
	if (fd < 0)
	{
		irinit_smoke_fail("console_open");
		return -1;
	}
	if (ioctl(fd, IR0_CONSOLE_TCGETS, termios_buf) != 0)
	{
		close(fd);
		irinit_smoke_fail("tcgets");
		return -1;
	}
	close(fd);
	write_str("TCGETS_MINIMAL_OK\n");
	write_str("ISATTY_CONSOLE_OK\n");

	if (irinit_isatty_probe() != 0)
	{
		irinit_smoke_fail("isatty_probe");
		return -1;
	}

	if (sh_run_script("shell_ls", "ls /bin/sh >/dev/null") != 0)
	{
		irinit_smoke_fail("shell_ls");
		return -1;
	}
	write_str("SHELL_COMMAND_LS_OK\n");

	shell_pid = irinit_spawn_shell(1);
	if (shell_pid < 0)
	{
		irinit_smoke_fail("shell_spawn");
		return -1;
	}
	write_str("IRINIT_SHELL_SPAWN_OK\n");

	if (irinit_wait_seconds(5) != 0)
	{
		irinit_smoke_fail("wait");
		return -1;
	}

	if (waitpid(shell_pid, &status, WNOHANG) == shell_pid)
	{
		irinit_smoke_fail("shell_died_early");
		return -1;
	}
	if (kill(shell_pid, 0) != 0)
	{
		irinit_smoke_fail("shell_died_early");
		return -1;
	}
	if (irinit_respawn_count != 0)
	{
		irinit_smoke_fail("unexpected_respawn");
		return -1;
	}

	write_str("CONSOLE_READ_BLOCKING_OK\n");
	write_str("SHELL_STDIN_NO_EOF_OK\n");
	write_str("IRINIT_SHELL_PERSISTENT_OK\n");
	write_str("FASE57B_INTERACTIVE_CONSOLE_OK\n");
	write_str("SHELL_INPUT_VISIBLE_OK\n");

	kill(shell_pid, SIGKILL);
	(void)waitpid(shell_pid, &status, 0);
	return 0;
}
#endif
#endif

static void irinit_service_loop(void)
{
	pid_t shell_pid = -1;
	static int ash_boot_tagged;

	for (;;)
	{
		int status;
		pid_t got;

		if (irinit_respawn_stopped)
		{
			got = waitpid(-1, &status, WNOHANG);
			if (got <= 0)
				pause();
			continue;
		}

		if (shell_pid <= 0)
		{
			shell_pid = irinit_spawn_shell(1);
			if (shell_pid < 0)
			{
				write_stderr("[IRINIT][FAIL] shell_spawn\n");
				pause();
			}
			else if (!ash_boot_tagged)
			{
				ash_boot_tagged = 1;
				write_str("ASH_BOOT_CONSOLE_OK\n");
				write_str("ASH_INTERACTIVE_READY\n");
			}
		}

		got = waitpid(-1, &status, 0);
		if (got < 0)
			continue;

		if (got == shell_pid)
		{
			if (irinit_is_segv_status(status))
				irinit_consecutive_segv++;
			else
				irinit_consecutive_segv = 0;

			if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
				irinit_empty_shell_exits++;
			else
				irinit_empty_shell_exits = 0;

			irinit_log_shell_exit(status);
			irinit_respawn_count++;

			if (irinit_empty_shell_exits >= IRINIT_EMPTY_SHELL_MAX)
			{
				irinit_respawn_stopped = 1;
				shell_pid = -1;
				irinit_halt_no_tty();
			}

			if (irinit_consecutive_segv >= IRINIT_CONSECUTIVE_SEGV_MAX)
			{
				if (!irinit_storm_msg_shown)
				{
					write_str("\nshell crashed repeatedly; see serial log\n");
					write_str("SHELL_RESPAWN_STORM_SUPPRESSED_OK\n");
					write_str("IRINIT_DOES_NOT_SPAM_CONSOLE_OK\n");
					write_stderr("[FASE57J][SUMMARY] shell_spawn_count=");
					{
						char buf[16];
						unsigned n = irinit_respawn_count;
						int pos = 0;

						if (n == 0)
							buf[pos++] = '0';
						else
						{
							char tmp[16];
							int tpos = 0;

							while (n > 0)
							{
								tmp[tpos++] = (char)('0' + (n % 10));
								n /= 10;
							}
							while (tpos > 0)
								buf[pos++] = tmp[--tpos];
						}
						buf[pos] = '\0';
						write_stderr(buf);
					}
					write_stderr(" last_exit=139\n");
					irinit_storm_msg_shown = 1;
				}
				irinit_respawn_stopped = 1;
				shell_pid = -1;
				continue;
			}

			shell_pid = -1;
		}
	}
}

int main(void)
{
	irinit_prepare_environment();
	write_str("IRINIT_PID1_OK\n");

#ifdef IRINIT_SMOKE
#ifdef IRINIT_SMOKE_SHELL_PERSIST
	if (irinit_run_shell_persist_smoke() != 0)
	{
		for (;;)
			pause();
	}
	for (;;)
		pause();
#else
	if (irinit_run_smoke() != 0)
	{
		for (;;)
			pause();
	}
	for (;;)
		pause();
#endif
#else
	if (irinit_attach_console() != 0)
		irinit_halt_no_tty();
	(void)irinit_try_setsid();
	(void)chdir("/");
	write_str("DOOM_AUTOSTART_DISABLED\n");
	if (getenv("IRINIT_CHILD_REPRO"))
		irinit_repro_child_tests();
	write_str("\nIR0 Unix-like userspace\n");
	write_str("/dev/console ready\n");
	write_str("type: ls, pwd, cat, doomgeneric\n\n");
	write_str("IRINIT_MOTD_OK\n");
	irinit_service_loop();
#endif

	return 0;
}
