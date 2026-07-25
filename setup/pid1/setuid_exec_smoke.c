/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: setuid_exec_smoke.c
 * Description: PID1 driver for the setuid/setgid-on-exec credential contract.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <sys/wait.h>
#include <unistd.h>
#include <stdint.h>

#define SYS_geteuid 107
#define SYS_setresuid 117
#define SYS_setresgid 119
#define SYS_prctl 157
#define SYS_exit_group 231

#define PR_SET_NO_NEW_PRIVS 38

#define TEST_UID 1000
#define TEST_GID 1000

static long ir0_syscall3(long nr, long a, long b, long c)
{
	long ret;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b), "d"(c)
		: "rcx", "r11", "memory");

	return ret;
}

/* prctl(2) rejects non-zero trailing arguments, so all five must be explicit. */
static long ir0_syscall5(long nr, long a, long b, long c, long d, long e)
{
	long ret;
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;

	__asm__ volatile(
		"syscall"
		: "=a"(ret)
		: "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8)
		: "rcx", "r11", "memory");

	return ret;
}

static void put(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	(void)write(1, s, n);
}

static void halt(int code)
{
	ir0_syscall3(SYS_exit_group, code, 0, 0);
	for (;;)
		__asm__ volatile("hlt");
}

static void child_fail(const char *what, int code)
{
	put("SETID_CASE_FAIL ");
	put(what);
	put("\n");
	halt(code);
}

/* Drop to the unprivileged test identity; group first, while still root. */
static int drop_privileges(void)
{
	if (ir0_syscall3(SYS_setresgid, TEST_GID, TEST_GID, TEST_GID) != 0)
		return -1;
	if (ir0_syscall3(SYS_setresuid, TEST_UID, TEST_UID, TEST_UID) != 0)
		return -1;
	return 0;
}

static int wait_child_ok(pid_t pid)
{
	int status = 0;

	if (waitpid(pid, &status, 0) != pid)
		return -1;
	if ((status & 0x7f) != 0)
		return -1;
	return ((status >> 8) & 0xff) == 0 ? 0 : -1;
}

/*
 * Positive case: exec @path as the unprivileged user and let the helper assert
 * the effective IDs it inherited from the image.
 */
static int case_exec(const char *path, char *want_euid, char *want_egid,
		     char *tag, int no_new_privs)
{
	pid_t pid = fork();

	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		char *argv[5];
		char *envp[1];

		if (drop_privileges() != 0)
			child_fail("drop", 11);
		if (no_new_privs &&
		    ir0_syscall5(SYS_prctl, PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0)
			child_fail("no_new_privs", 12);

		argv[0] = (char *)path;
		argv[1] = want_euid;
		argv[2] = want_egid;
		argv[3] = tag;
		argv[4] = NULL;
		envp[0] = NULL;

		execve(path, argv, envp);
		put("SETID_CASE_FAIL exec ");
		put(path);
		put("\n");
		halt(13);
	}

	return wait_child_ok(pid);
}

/*
 * Negative case: exec of @path must fail and leave the caller unprivileged.
 */
static int case_exec_denied(const char *path, const char *tag)
{
	pid_t pid = fork();

	if (pid < 0)
		return -1;

	if (pid == 0)
	{
		char *argv[2];
		char *envp[1];

		if (drop_privileges() != 0)
			child_fail("drop", 21);

		argv[0] = (char *)path;
		argv[1] = NULL;
		envp[0] = NULL;

		execve(path, argv, envp);

		if (ir0_syscall3(SYS_geteuid, 0, 0, 0) != TEST_UID)
			child_fail("euid_after_failed_exec", 22);
		put(tag);
		put("\n");
		halt(0);
	}

	return wait_child_ok(pid);
}

static void parent_fail(const char *what, int code)
{
	put("SETUID_EXEC_FAIL case=");
	put(what);
	put("\n");
	halt(code);
}

int main(void)
{
	if (ir0_syscall3(SYS_geteuid, 0, 0, 0) != 0)
	{
		put("SETUID_EXEC_FAIL not_root\n");
		halt(1);
	}

	if (case_exec("/bin/setid_suid", "0", "1000", "SETID_SUID_OK", 0) != 0)
		parent_fail("suid", 2);
	if (case_exec("/bin/setid_sgid", "1000", "0", "SETID_SGID_OK", 0) != 0)
		parent_fail("sgid", 3);
	if (case_exec("/bin/setid_plain", "1000", "1000", "SETID_PLAIN_OK", 0) != 0)
		parent_fail("plain", 4);
	if (case_exec("/bin/setid_suid", "1000", "1000", "SETID_NNP_OK", 1) != 0)
		parent_fail("no_new_privs", 5);
	if (case_exec_denied("/bin/setid_missing", "SETID_ENOENT_OK") != 0)
		parent_fail("enoent", 6);
	if (case_exec_denied("/bin/setid_script", "SETID_SCRIPT_OK") != 0)
		parent_fail("script", 7);
	if (case_exec_denied("/bin/setid_priv", "SETID_NOEXEC_OK") != 0)
		parent_fail("noexec", 8);

	put("SETUID_EXEC_ALL_OK\n");
	halt(0);
	return 0;
}
