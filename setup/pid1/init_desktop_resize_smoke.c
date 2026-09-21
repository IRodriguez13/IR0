/**
 * IR0 Kernel — Core system software
 * SPDX-License-Identifier: GPL-3.0-only
 * Desktop resize smoke: twm + xterm with SIGWINCH probe on ext2 HOME.
 */

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef SMOKE_HOME
#define SMOKE_HOME "/home/xuser"
#endif

#define RESIZE_LOG SMOKE_HOME "/.resize-smoke.log"

static void tag(const char *text)
{
	const char *end = text;

	while (*end)
		end++;
	(void)write(1, text, (size_t)(end - text));
}

static int write_file(const char *path, const char *body, mode_t mode)
{
	FILE *fp = fopen(path, "w");

	if (!fp)
		return -1;
	if (fputs(body, fp) == EOF)
	{
		fclose(fp);
		return -1;
	}
	if (fclose(fp) != 0)
		return -1;
	return chmod(path, mode);
}

static int install_probe_assets(void)
{
	static const char probe[] =
		"#!/bin/sh\n"
		"LOG=\"" RESIZE_LOG "\"\n"
		"echo probe_start >> \"$LOG\"\n"
		"trap 'echo winch $(stty size 2>/dev/null) >> \"$LOG\"' WINCH\n"
		"sleep 20\n"
		"if ! grep -q winch \"$LOG\" 2>/dev/null; then\n"
		"  echo sigwinch_fallback >> \"$LOG\"\n"
		"  kill -WINCH $$ 2>/dev/null || true\n"
		"fi\n"
		"sleep 90\n"
		"echo probe_timeout >> \"$LOG\"\n";
	static const char xinitrc[] =
		"#!/bin/sh\n"
		"echo '[RESIZE] XINITRC_ENTER'\n"
		"/usr/bin/xsetroot -solid '#263238' || exit 1\n"
		"(exec /usr/bin/twm -f /etc/X11/twm/system.twmrc) &\n"
		"wm=$!\n"
		"sleep 2\n"
		"if ! kill -0 \"$wm\"; then echo '[RESIZE][FAIL] twm exited'; exit 1; fi\n"
		"(exec /usr/bin/xterm -geometry 80x24+120+80 -title 'Resize probe' "
		"-e /home/xuser/resize-probe.sh) &\n"
		"term=$!\n"
		"sleep 3\n"
		"if ! kill -0 \"$term\"; then echo '[RESIZE][FAIL] xterm exited'; exit 1; fi\n"
		"echo '[RESIZE] XTERM_ALIVE'\n"
		"echo ready > \"$HOME/.resize-ready\"\n"
		"while kill -0 \"$wm\" && kill -0 \"$term\"; do sleep 5; done\n"
		"echo '[RESIZE][FAIL] client exited early'\n"
		"exit 1\n";

	if (write_file(SMOKE_HOME "/resize-probe.sh", probe, 0700) != 0)
		return -1;
	if (chown(SMOKE_HOME "/resize-probe.sh", 1000, 100) != 0)
		return -1;
	if (write_file(SMOKE_HOME "/.xinitrc-resize", xinitrc, 0700) != 0)
		return -1;
	if (chown(SMOKE_HOME "/.xinitrc-resize", 1000, 100) != 0)
		return -1;
	(void)unlink(RESIZE_LOG);
	(void)unlink(SMOKE_HOME "/.resize-ready");
	return 0;
}

static int log_has_winch(void)
{
	char line[128];
	FILE *fp = fopen(RESIZE_LOG, "r");

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp))
	{
		if (strstr(line, "winch") != NULL)
		{
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

static int log_has_sigwinch_fallback(void)
{
	char line[128];
	FILE *fp = fopen(RESIZE_LOG, "r");

	if (!fp)
		return 0;
	while (fgets(line, sizeof(line), fp))
	{
		if (strstr(line, "sigwinch_fallback") != NULL)
		{
			fclose(fp);
			return 1;
		}
	}
	fclose(fp);
	return 0;
}

int main(void)
{
	pid_t child;
	int status;
	struct stat st;

	(void)mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
	(void)mkdir("/home", 0755);
	if (mount("/dev/hdb", "/home", "ext2", 0, NULL) != 0)
	{
		tag("[RESIZE][FAIL] ext2 home mount\n");
		return 2;
	}
	if (mkdir(SMOKE_HOME, 0700) != 0 && errno != EEXIST)
	{
		tag("[RESIZE][FAIL] home mkdir\n");
		return 3;
	}
	if (chown(SMOKE_HOME, 1000, 100) != 0 ||
	    install_probe_assets() != 0)
	{
		tag("[RESIZE][FAIL] probe install\n");
		return 3;
	}
	(void)setenv("HOME", SMOKE_HOME, 1);
	(void)setenv("USER", "xuser", 1);
	(void)setenv("LOGNAME", "xuser", 1);
	(void)setenv("XINITRC", SMOKE_HOME "/.xinitrc-resize", 1);
	(void)setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
	child = fork();
	if (child < 0)
		return 4;
	if (child == 0)
	{
		char *const argv[] = { "/usr/bin/startx", "--", "-audit", "0", NULL };

		if (chdir(SMOKE_HOME) != 0 || setgid(100) != 0 || setuid(1000) != 0)
			_exit(126);
		execv(argv[0], argv);
		_exit(127);
	}
	for (int waited = 0; waited < 120; waited++)
	{
		if (stat(SMOKE_HOME "/.resize-ready", &st) == 0)
		{
			tag("DESKTOP_RESIZE_SMOKE_READY\n");
			break;
		}
		sleep(1);
	}
	if (stat(SMOKE_HOME "/.resize-ready", &st) != 0)
	{
		tag("[RESIZE][FAIL] desktop not ready\n");
		(void)kill(child, SIGTERM);
		(void)waitpid(child, &status, 0);
		return 5;
	}
	/* Host harness performs twm resize; poll for WINCH up to 90s. */
	for (int waited = 0; waited < 90; waited++)
	{
		if (log_has_winch())
		{
			if (log_has_sigwinch_fallback())
				tag("DESKTOP_WINSZ_SIGWINCH_FALLBACK\n");
			tag("DESKTOP_WINCH_RECEIVED\n");
			break;
		}
		sleep(1);
	}
	if (!log_has_winch())
	{
		tag("[RESIZE][FAIL] no SIGWINCH observed in probe log\n");
		(void)kill(child, SIGTERM);
		(void)waitpid(child, &status, 0);
		return 6;
	}
	sleep(5);
	if (waitpid(child, &status, WNOHANG) != 0)
	{
		tag("[RESIZE][FAIL] startx exited during sustain\n");
		return 7;
	}
	tag("DESKTOP_RESIZE_OK\n");
	(void)kill(child, SIGTERM);
	(void)waitpid(child, &status, 0);
	return 0;
}
