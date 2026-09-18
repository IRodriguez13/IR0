/**
 * IR0 Kernel — Core system software
 * SPDX-License-Identifier: GPL-3.0-only
 * Automated product smoke: upstream startx with a writable ext2 HOME.
 */

#include <errno.h>
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

static void tag(const char *text)
{
	const char *end = text;

	while (*end)
		end++;
	(void)write(1, text, (size_t)(end - text));
}

static void child_failed(int sig)
{
	(void)sig;
	tag("[STARTX][FAIL] session exited\n");
	_exit(5);
}

static int probe_x11_listener(void)
{
	struct stat st;

	if (stat("/tmp/.X11-unix/X0", &st) != 0 || !S_ISSOCK(st.st_mode))
	{
		tag("[STARTX][FAIL] pathname socket inode missing\n");
		return -1;
	}
	tag("[STARTX] X11_PATH_SOCKET_INODE_OK\n");
	return 0;
}

static int install_smoke_xinitrc(void)
{
	static const char script[] =
		"#!/bin/sh\n"
		"echo '[STARTX] XINITRC_ENTER'\n"
		"if [ -f /usr/share/backgrounds/ir0-desktop.xbm ]; then\n"
		"  /usr/bin/xsetroot -bitmap /usr/share/backgrounds/ir0-desktop.xbm -fg '#78909c' -bg '#1b2830' || { echo '[STARTX][FAIL] xsetroot exited'; exit 1; }\n"
		"else\n"
		"  /usr/bin/xsetroot -mod 3 3 -fg '#78909c' -bg '#263238' || { echo '[STARTX][FAIL] xsetroot exited'; exit 1; }\n"
		"fi\n"
		"echo ready > $HOME/.xsetroot-ready\n"
		"echo '[STARTX] XSETROOT_EXEC_OK'\n"
		"(echo '[STARTX] TWM_EXEC'; exec /usr/bin/twm) 2>$HOME/.twm-smoke.err &\n"
		"wm=$!\n"
		"sleep 3\n"
		"if ! kill -0 \"$wm\"; then echo '[STARTX][FAIL] twm exited'; cat $HOME/.twm-smoke.err; exit 1; fi\n"
		"(echo '[STARTX] XCLOCK_EXEC'; /usr/bin/xclock -digital -update 1 -geometry 220x48-18+18; rc=$?; echo \"[STARTX] XCLOCK_EXIT=$rc\"; exit $rc) 2>$HOME/.xclock-smoke.err &\n"
		"clock=$!\n"
		"(echo '[STARTX] XEYES_EXEC'; exec /usr/bin/xeyes -geometry 150x90-22+150) 2>$HOME/.xeyes-smoke.err &\n"
		"eyes=$!\n"
		"(echo '[STARTX] XLOGO_EXEC'; exec /usr/bin/xlogo -geometry 160x120-24-30) 2>$HOME/.xlogo-smoke.err &\n"
		"logo=$!\n"
		"(echo '[STARTX] XCALC_EXEC'; /usr/bin/xcalc -geometry 226x304-210+170; rc=$?; echo \"[STARTX] XCALC_EXIT=$rc\"; exit $rc) &\n"
		"calc=$!\n"
		"(echo '[STARTX] XMESSAGE_EXEC'; /usr/bin/xmessage -timeout 120 -geometry +24+180 -buttons Close:0 'IR0 Xaw smoke'; rc=$?; echo \"[STARTX] XMESSAGE_EXIT=$rc\"; exit $rc) &\n"
		"message=$!\n"
		"load=0\n"
		"if [ -x /usr/bin/xload ]; then\n"
		"  (echo '[STARTX] XLOAD_EXEC'; exec /usr/bin/xload -geometry 220x90-18+250) 2>$HOME/.xload-smoke.err &\n"
		"  load=$!\n"
		"fi\n"
		"sleep 1\n"
		"(echo '[STARTX] XTERM_EXEC'; exec /usr/bin/xterm -geometry 80x24+400+250) 2>$HOME/.xterm-smoke.err &\n"
		"terminal=$!\n"
		"sleep 2\n"
		"if ! kill -0 \"$terminal\"; then echo '[STARTX][FAIL] xterm exited'; cat $HOME/.xterm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$clock\"; then echo '[STARTX][FAIL] xclock exited'; cat $HOME/.xclock-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$eyes\"; then echo '[STARTX][FAIL] xeyes exited'; cat $HOME/.xeyes-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$logo\"; then echo '[STARTX][FAIL] xlogo exited'; cat $HOME/.xlogo-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$calc\"; then echo '[STARTX][FAIL] xcalc exited'; cat $HOME/.xcalc-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$message\"; then echo '[STARTX][FAIL] xmessage exited'; cat $HOME/.xmessage-smoke.err; exit 1; fi\n"
		"if [ \"$load\" != 0 ] && ! kill -0 \"$load\"; then echo '[STARTX][FAIL] xload exited'; cat $HOME/.xload-smoke.err; exit 1; fi\n"
		"echo ready > \"$HOME/.xclient-started\"\n"
		"sleep 12\n"
		"if ! kill -0 \"$wm\"; then echo '[STARTX][FAIL] twm exited after readiness'; cat $HOME/.twm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$terminal\"; then echo '[STARTX][FAIL] xterm exited after readiness'; cat $HOME/.xterm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$clock\"; then echo '[STARTX][FAIL] xclock exited after readiness'; cat $HOME/.xclock-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$eyes\"; then echo '[STARTX][FAIL] xeyes exited after readiness'; cat $HOME/.xeyes-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$logo\"; then echo '[STARTX][FAIL] xlogo exited after readiness'; cat $HOME/.xlogo-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$calc\"; then echo '[STARTX][FAIL] xcalc exited after readiness'; cat $HOME/.xcalc-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$message\"; then echo '[STARTX][FAIL] xmessage exited after readiness'; cat $HOME/.xmessage-smoke.err; exit 1; fi\n"
		"if [ \"$load\" != 0 ] && ! kill -0 \"$load\"; then echo '[STARTX][FAIL] xload exited after readiness'; cat $HOME/.xload-smoke.err; exit 1; fi\n"
		"echo sustained > \"$HOME/.xclient-sustained\"\n"
		"while kill -0 \"$wm\" && kill -0 \"$terminal\" && kill -0 \"$clock\" && kill -0 \"$eyes\" && kill -0 \"$logo\" && kill -0 \"$calc\"; do sleep 60; done\n";
	static const char twmrc[] = "RandomPlacement\n";
	FILE *fp = fopen(SMOKE_HOME "/.xinitrc-smoke", "w");

	if (!fp)
		return -1;
	if (fwrite(script, 1, sizeof(script) - 1, fp) != sizeof(script) - 1 ||
	    fclose(fp) != 0)
		return -1;
	if (chown(SMOKE_HOME "/.xinitrc-smoke", 1000, 100) != 0 ||
	    chmod(SMOKE_HOME "/.xinitrc-smoke", 0700) != 0)
		return -1;
	fp = fopen(SMOKE_HOME "/.twmrc", "w");
	if (!fp)
		return -1;
	if (fwrite(twmrc, 1, sizeof(twmrc) - 1, fp) != sizeof(twmrc) - 1 ||
	    fclose(fp) != 0)
		return -1;
	if (chown(SMOKE_HOME "/.twmrc", 1000, 100) != 0 ||
	    chmod(SMOKE_HOME "/.twmrc", 0600) != 0)
		return -1;
	return 0;
}

int main(void)
{
	pid_t child;
	int status;
	struct sigaction sa;

	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = child_failed;
	(void)sigaction(SIGCHLD, &sa, NULL);

	(void)mount("tmpfs", "/tmp", "tmpfs", 0, "mode=1777");
	(void)chmod("/tmp", 01777);
	(void)mkdir("/home", 0755);
	if (mount("/dev/hdb", "/home", "ext2", 0, NULL) != 0)
	{
		perror("mount ext2 home");
		tag("[STARTX][FAIL] ext2 home mount\n");
		return 2;
	}
	if (mkdir(SMOKE_HOME, 0700) != 0 && errno != EEXIST)
	{
		tag("[STARTX][FAIL] home mkdir\n");
		return 3;
	}
	if (chown(SMOKE_HOME, 1000, 100) != 0)
	{
		tag("[STARTX][FAIL] home chown\n");
		return 3;
	}
	if (install_smoke_xinitrc() != 0)
	{
		tag("[STARTX][FAIL] xinitrc install\n");
		return 3;
	}
	(void)unlink(SMOKE_HOME "/.xclient-started");
	(void)unlink(SMOKE_HOME "/.xclient-sustained");
	(void)unlink(SMOKE_HOME "/.xsetroot-ready");
	(void)setenv("HOME", SMOKE_HOME, 1);
	(void)setenv("USER", "xuser", 1);
	(void)setenv("LOGNAME", "xuser", 1);
	(void)setenv("XINITRC", SMOKE_HOME "/.xinitrc-smoke", 1);
	(void)setenv("PATH", "/usr/bin:/bin:/usr/sbin:/sbin", 1);
	child = fork();
	if (child < 0)
		return 4;
	if (child == 0)
	{
		char *const argv[] = {
			"/usr/bin/startx", "--", "-audit", "4", NULL
		};

		(void)signal(SIGCHLD, SIG_DFL);

		if (chdir(SMOKE_HOME) != 0 ||
		    setgid(100) != 0 || setuid(1000) != 0)
			_exit(126);
		execv(argv[0], argv);
		_exit(127);
	}
	sleep(3);
	if (probe_x11_listener() != 0)
		return 6;
	for (status = 0; status < 60; status++)
	{
		if (access(SMOKE_HOME "/.xclient-started", F_OK) == 0)
			break;
		sleep(1);
	}
	if (status == 60)
	{
		tag("[STARTX][FAIL] X client was not launched\n");
		return 7;
	}
	for (status = 0; status < 30; status++)
	{
		if (access(SMOKE_HOME "/.xclient-sustained", F_OK) == 0)
			break;
		sleep(1);
	}
	if (status == 30)
	{
		tag("[STARTX][FAIL] WM/terminal did not remain alive\n");
		return 8;
	}
	if (waitpid(child, &status, WNOHANG) == child || kill(child, 0) != 0)
	{
		(void)waitpid(child, &status, 0);
		tag("[STARTX][FAIL] session exited\n");
		return 5;
	}
	tag("[STARTX] CLASSIFY UPSTREAM_STARTX_EXT2_HOME_OK\n");
	tag("[STARTX] X11_WM_AND_TERMINAL_LAUNCHED_OK\n");
	tag("[STARTX] X11_WM_AND_TERMINAL_SUSTAINED_OK\n");
	tag("[STARTX] X11_DESKTOP_BACKGROUND_OK\n");
	tag("[STARTX] X11_DESKTOP_CLIENTS_SUSTAINED_OK\n");
	tag("[STARTX] X11_DESKTOP_DEMOS_OK\n");
	tag("[STARTX] X11_XAW_CLIENTS_OK\n");
	tag("[STARTX_EXT2_OK]\n");
	for (;;)
	{
		if (access(SMOKE_HOME "/xok", F_OK) == 0)
		{
			tag("[STARTX] X11_KEYBOARD_COMMAND_OK\n");
			for (;;)
				pause();
		}
		sleep(1);
	}
}
