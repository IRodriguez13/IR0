/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kd_keymap.c
 * Description: KDGKBENT/KDSKBENT round-trip one keymap entry on /dev/tty0.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"

#ifndef SYS_ioctl
#define SYS_ioctl		29
#endif

static int kd_ioctl(int fd, unsigned long req, void *arg)
{
	return (int)syscall(SYS_ioctl, fd, (long)req, (long)arg, 0);
}

static int kd_errno_is_einval(void)
{
	return errno == 22;
}
#else
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int kd_ioctl(int fd, unsigned long req, void *arg)
{
	return ioctl(fd, req, arg);
}

static int kd_errno_is_einval(void)
{
	return errno == EINVAL;
}
#endif

#include "ir0/ksft.h"

/* include/uapi/linux/kd.h and keyboard.h */
#define KD_GKBTYPE		0x4B33
#define KD_KB_101		0x02
#define KD_GKBENT		0x4B46
#define KD_SKBENT		0x4B47
#define KD_NORMTAB		0
#define KD_KEY_Q		16
#define KD_SYM(t, v)		((unsigned short)(((t) << 8) | (v)))
#define KD_KT_CUR		6
#define KD_KT_LETTER		11

struct kd_kbentry
{
	unsigned char kb_table;
	unsigned char kb_index;
	unsigned short kb_value;
};

static void kd_entry_init(struct kd_kbentry *ent, unsigned short value)
{
	volatile unsigned char *p = (volatile unsigned char *)ent;
	unsigned int i;

	for (i = 0; i < sizeof(*ent); i++)
		p[i] = 0;
	ent->kb_table = KD_NORMTAB;
	ent->kb_index = KD_KEY_Q;
	ent->kb_value = value;
}

static int kd_read_entry(int fd, unsigned short *value)
{
	struct kd_kbentry ent;

	kd_entry_init(&ent, 0);
	if (kd_ioctl(fd, KD_GKBENT, &ent) != 0)
		return -1;
	*value = ent.kb_value;
	return 0;
}

static int kd_write_entry(int fd, unsigned short value)
{
	struct kd_kbentry ent;

	kd_entry_init(&ent, value);
	return kd_ioctl(fd, KD_SKBENT, &ent);
}

static int kd_keymap_check(int fd)
{
	unsigned short orig;
	unsigned short want;
	unsigned short got;
	unsigned char type;
	int rc;
	int bad;

	type = 0;
	if (kd_ioctl(fd, KD_GKBTYPE, &type) != 0 || type != KD_KB_101)
		return 0;
	if (kd_read_entry(fd, &orig) != 0)
		return 0;
	want = orig == KD_SYM(KD_KT_LETTER, 'x') ? KD_SYM(KD_KT_LETTER, 'y')
						  : KD_SYM(KD_KT_LETTER, 'x');
	if (kd_write_entry(fd, want) != 0)
		return 0;
	rc = kd_read_entry(fd, &got) == 0 && got == want;

	/* KT_CUR has four values; Linux rejects K(KT_CUR, 4). */
	bad = kd_write_entry(fd, KD_SYM(KD_KT_CUR, 4));
	rc = rc && bad == -1 && kd_errno_is_einval();
	rc = rc && kd_read_entry(fd, &got) == 0 && got == want;

	if (kd_write_entry(fd, orig) != 0)
		return 0;
	return rc && kd_read_entry(fd, &got) == 0 && got == orig;
}

int main(void)
{
	int fd;
	int pass;

	ksft_set_plan(1);
	pass = 0;
	fd = open("/dev/tty0", 2);
	if (fd >= 0)
	{
		pass = kd_keymap_check(fd);
		(void)close(fd);
	}
	ksft_test_result(pass, "kd_keymap");
	return ksft_exit();
}
