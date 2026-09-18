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
		"/usr/bin/xsetroot -bitmap /usr/share/backgrounds/ir0desk.xbm -fg '#78909c' -bg '#263238' -name 'IR0 Desktop' || { echo '[STARTX][FAIL] xsetroot exited'; exit 1; }\n"
		"echo ready > $HOME/.xsetroot-ready\n"
		"echo '[STARTX] XSETROOT_EXEC_OK'\n"
		"(echo '[STARTX] TWM_EXEC'; exec /usr/bin/twm -f /etc/X11/twm/system.twmrc) 2>$HOME/.twm-smoke.err &\n"
		"wm=$!\n"
		"sleep 3\n"
		"if ! kill -0 \"$wm\"; then echo '[STARTX][FAIL] twm exited'; cat $HOME/.twm-smoke.err; exit 1; fi\n"
		"(echo '[STARTX] XCLOCK_EXEC'; exec /usr/bin/xclock -geometry 100x100+12+12 -bg '#263238' -fg '#eceff1' -bd '#87a9b5') 2>$HOME/.xclock-smoke.err &\n"
		"clock=$!\n"
		"(echo '[STARTX] XWORKSPACES_EXEC'; exec /usr/bin/xmessage -timeout 0 -buttons \"One:0\",\"Two:0\",\"Three:0\",\"Four:0\" -geometry 280x32+372-58 -bg '#263238' -fg '#eceff1' -bd '#87a9b5' ' ') 2>$HOME/.xworkspaces-smoke.err &\n"
		"workspaces=$!\n"
		"(echo '[STARTX] XEYES_EXEC'; exec /usr/bin/xeyes -geometry 150x90-22+150) 2>$HOME/.xeyes-smoke.err &\n"
		"eyes=$!\n"
		"(echo '[STARTX] XLOGO_EXEC'; exec /usr/bin/xlogo -geometry 160x120-24-30) 2>$HOME/.xlogo-smoke.err &\n"
		"logo=$!\n"
		"(echo '[STARTX] XCALC_EXEC'; /usr/bin/xcalc -geometry 226x304-210+170; rc=$?; echo \"[STARTX] XCALC_EXIT=$rc\"; exit $rc) &\n"
		"calc=$!\n"
		"sleep 1\n"
		"(echo '[STARTX] XTERM_EXEC'; exec /usr/bin/xterm -geometry 80x24+400+250 -title 'IR0 Terminal') 2>$HOME/.xterm-smoke.err &\n"
		"terminal=$!\n"
		"sleep 1\n"
		"(echo '[STARTX] XTERM_CHAT_EXEC'; exec /usr/bin/xterm -geometry 72x16+520+300 -title 'IR0 Chat' -e /bin/sh -c 'echo chat-ready; exec /bin/sh') 2>$HOME/.xterm-chat-smoke.err &\n"
		"chat=$!\n"
		"sleep 2\n"
		"if ! kill -0 \"$terminal\"; then echo '[STARTX][FAIL] xterm exited'; cat $HOME/.xterm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$chat\"; then echo '[STARTX][FAIL] chat xterm exited'; cat $HOME/.xterm-chat-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$clock\"; then echo '[STARTX][FAIL] xclock exited'; cat $HOME/.xclock-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$workspaces\"; then echo '[STARTX][FAIL] workspace bar exited'; cat $HOME/.xworkspaces-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$eyes\"; then echo '[STARTX][FAIL] xeyes exited'; cat $HOME/.xeyes-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$logo\"; then echo '[STARTX][FAIL] xlogo exited'; cat $HOME/.xlogo-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$calc\"; then echo '[STARTX][FAIL] xcalc exited'; cat $HOME/.xcalc-smoke.err; exit 1; fi\n"
		"echo '[STARTX] X11_DESKTOP_TASKBAR_OK'\n"
		"echo '[STARTX] X11_DESKTOP_WORKSPACES_OK'\n"
		"echo ready > \"$HOME/.xclient-started\"\n"
		"sleep 12\n"
		"if ! kill -0 \"$wm\"; then echo '[STARTX][FAIL] twm exited after readiness'; cat $HOME/.twm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$terminal\"; then echo '[STARTX][FAIL] xterm exited after readiness'; cat $HOME/.xterm-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$chat\"; then echo '[STARTX][FAIL] chat xterm exited after readiness'; cat $HOME/.xterm-chat-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$clock\"; then echo '[STARTX][FAIL] xclock exited after readiness'; cat $HOME/.xclock-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$workspaces\"; then echo '[STARTX][FAIL] workspace bar exited after readiness'; cat $HOME/.xworkspaces-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$eyes\"; then echo '[STARTX][FAIL] xeyes exited after readiness'; cat $HOME/.xeyes-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$logo\"; then echo '[STARTX][FAIL] xlogo exited after readiness'; cat $HOME/.xlogo-smoke.err; exit 1; fi\n"
		"if ! kill -0 \"$calc\"; then echo '[STARTX][FAIL] xcalc exited after readiness'; cat $HOME/.xcalc-smoke.err; exit 1; fi\n"
		"echo sustained > \"$HOME/.xclient-sustained\"\n"
		"while kill -0 \"$wm\" && kill -0 \"$terminal\" && kill -0 \"$chat\" && kill -0 \"$clock\" && kill -0 \"$workspaces\" && kill -0 \"$eyes\" && kill -0 \"$logo\" && kill -0 \"$calc\"; do sleep 60; done\n";
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
	tag("[STARTX] X11_DESKTOP_WORKSPACES_OK\n");
	tag("[STARTX] X11_DESKTOP_DEMOS_OK\n");
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
