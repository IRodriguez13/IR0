/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: setid_helper.c
 * Description: exec target for the setuid/setgid-on-exec contract smoke.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <unistd.h>
#include <stdint.h>

#define SYS_getuid 102
#define SYS_getgid 104
#define SYS_geteuid 107
#define SYS_getegid 108
#define SYS_setresuid 117
#define SYS_getresuid 118
#define SYS_exit_group 231

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

static void put(const char *s)
{
	size_t n = 0;

	while (s[n])
		n++;
	(void)write(1, s, n);
}

static void put_long(long v)
{
	char buf[24];
	int i = (int)sizeof(buf);
	unsigned long u = v < 0 ? (unsigned long)-v : (unsigned long)v;

	buf[--i] = '\0';
	do
	{
		buf[--i] = (char)('0' + (u % 10));
		u /= 10;
	} while (u && i > 1);
	if (v < 0)
		buf[--i] = '-';
	put(&buf[i]);
}

static void halt(int code)
{
	ir0_syscall3(SYS_exit_group, code, 0, 0);
	for (;;)
		__asm__ volatile("hlt");
}

static long parse_long(const char *s)
{
	long v = 0;

	if (!s)
		return -1;
	while (*s >= '0' && *s <= '9')
	{
		v = v * 10 + (*s - '0');
		s++;
	}
	return v;
}

static void fail(const char *tag, const char *what, long got, long want)
{
	put("SETID_HELPER_FAIL tag=");
	put(tag);
	put(" ");
	put(what);
	put("=");
	put_long(got);
	put(" want=");
	put_long(want);
	put("\n");
	halt(1);
}

/*
 * Saved-UID contract: a program that gained euid 0 through the set-user-ID bit
 * must be able to drop to its real UID and come back, because the saved UID
 * still holds 0 (POSIX setresuid(2)).
 */
static void check_saved_uid_roundtrip(const char *tag, long real_uid)
{
	if (ir0_syscall3(SYS_setresuid, -1, real_uid, -1) != 0)
		fail(tag, "drop_euid", -1, 0);
	if (ir0_syscall3(SYS_geteuid, 0, 0, 0) != real_uid)
		fail(tag, "euid_after_drop", ir0_syscall3(SYS_geteuid, 0, 0, 0),
		     real_uid);
	if (ir0_syscall3(SYS_setresuid, -1, 0, -1) != 0)
		fail(tag, "regain_euid", -1, 0);
	if (ir0_syscall3(SYS_geteuid, 0, 0, 0) != 0)
		fail(tag, "euid_after_regain",
		     ir0_syscall3(SYS_geteuid, 0, 0, 0), 0);
}

int main(int argc, char **argv)
{
	long want_euid;
	long want_egid;
	const char *tag;
	long uid;
	long euid;
	long gid;
	long egid;
	long saved_uid = -1;

	if (argc < 4)
	{
		put("SETID_HELPER_FAIL usage\n");
		halt(2);
	}

	want_euid = parse_long(argv[1]);
	want_egid = parse_long(argv[2]);
	tag = argv[3];

	uid = ir0_syscall3(SYS_getuid, 0, 0, 0);
	euid = ir0_syscall3(SYS_geteuid, 0, 0, 0);
	gid = ir0_syscall3(SYS_getgid, 0, 0, 0);
	egid = ir0_syscall3(SYS_getegid, 0, 0, 0);

	if (euid != want_euid)
		fail(tag, "euid", euid, want_euid);
	if (egid != want_egid)
		fail(tag, "egid", egid, want_egid);

	/* execve must never change the real IDs. */
	if (uid == 0 && want_euid == 0 && want_egid == 0)
	{
		put("SETID_HELPER_FAIL real_uid_lost\n");
		halt(3);
	}

	{
		/* getresuid(2) writes uid_t (32-bit), not long. */
		unsigned int r = 0;
		unsigned int e = 0;
		unsigned int s = 0;

		if (ir0_syscall3(SYS_getresuid, (long)(uintptr_t)&r,
				 (long)(uintptr_t)&e,
				 (long)(uintptr_t)&s) != 0)
			fail(tag, "getresuid", -1, 0);
		if ((long)r != uid)
			fail(tag, "getresuid_real", (long)r, uid);
		if ((long)s != euid)
			fail(tag, "saved_uid", (long)s, euid);
		saved_uid = (long)s;
	}

	if (euid == 0 && uid != 0)
		check_saved_uid_roundtrip(tag, uid);

	put(tag);
	put(" uid=");
	put_long(uid);
	put(" euid=");
	put_long(euid);
	put(" gid=");
	put_long(gid);
	put(" egid=");
	put_long(egid);
	put(" suid=");
	put_long(saved_uid);
	put("\n");
	halt(0);
	return 0;
}
