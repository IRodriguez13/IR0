/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: rt.c
 * Description: Freestanding runtime for the conformance runner and the
 *              syscall fuzzer (see rt.h).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "rt.h"

#include <dirent.h>
#include <time.h>

/* Like musl: every handler returns through an rt_sigreturn trampoline. */
void rt_restore(void);
#if defined(__x86_64__)
__asm__(".text\n"
	".global rt_restore\n"
	".type rt_restore, @function\n"
	"rt_restore:\n"
	"	mov	$15, %eax\n"
	"	syscall\n"
	"	hlt\n");
#else
__asm__(".text\n"
	".global rt_restore\n"
	".type rt_restore, %function\n"
	"rt_restore:\n"
	"	mov	x8, #139\n"
	"	svc	#0\n");
#endif

#if defined(__aarch64__)
/*
 * No libc on arm64: the program brings its own entry and an in-image stack
 * (like tests/selftests/ir0/nolibc.h). x0 keeps the initial sp so argc and
 * argv still reach rt_main.
 */
unsigned char rt_stack[16384] __attribute__((aligned(16)));

void rt_start(long *sp) __attribute__((noreturn, used));

__asm__(".text\n"
	".global _start\n"
	".type _start, %function\n"
	"_start:\n"
	"	mov	x0, sp\n"
	"	adrp	x1, rt_stack\n"
	"	add	x1, x1, :lo12:rt_stack\n"
	"	add	sp, x1, #16384\n"
	"	bl	rt_start\n");

void rt_start(long *sp)
{
	long argc = sp ? sp[0] : 0;
	char **argv = (char **)(sp + 1);

	/* An exec that hands over no argument vector still runs the default. */
	if (argc < 0 || argc > 64)
		argc = 0;
	rt_exit(rt_main((int)argc, argc ? argv : 0));
}

/* The compiler may emit these for struct copies and initializers. */
void *memset(void *p, int byte, size_t n)
{
	rt_fill(p, byte, n);
	return p;
}

void *memcpy(void *dst, const void *src, size_t n)
{
	rt_copy(dst, src, n);
	return dst;
}
#else
int main(int argc, char **argv)
{
	return rt_main(argc, argv);
}
#endif

size_t rt_strlen(const char *s)
{
	size_t n = 0;

	while (s[n] != '\0')
		n++;
	return n;
}

int rt_streq(const char *a, const char *b)
{
	while (*a && *a == *b)
	{
		a++;
		b++;
	}
	return *a == *b;
}

int rt_memeq(const void *a, const void *b, size_t n)
{
	const volatile unsigned char *x = a;
	const volatile unsigned char *y = b;
	size_t i;

	for (i = 0; i < n; i++)
		if (x[i] != y[i])
			return 0;
	return 1;
}

void rt_fill(void *p, int byte, size_t n)
{
	volatile unsigned char *d = p;
	size_t i;

	for (i = 0; i < n; i++)
		d[i] = (unsigned char)byte;
}

void rt_copy(void *dst, const void *src, size_t n)
{
	volatile unsigned char *d = dst;
	const unsigned char *s = src;
	size_t i;

	for (i = 0; i < n; i++)
		d[i] = s[i];
}

char *rt_cat(char *dst, size_t cap, const char *s)
{
	size_t n = rt_strlen(dst);

	while (*s && n + 1 < cap)
		dst[n++] = *s++;
	dst[n] = '\0';
	return dst;
}

char *rt_catn(char *dst, size_t cap, long v)
{
	char tmp[24];
	unsigned long mag = v < 0 ? 0UL - (unsigned long)v : (unsigned long)v;
	int n = 0;
	char out[26];
	int k = 0;

	do
	{
		tmp[n++] = (char)('0' + mag % 10UL);
		mag /= 10UL;
	} while (mag);
	if (v < 0)
		out[k++] = '-';
	while (n)
		out[k++] = tmp[--n];
	out[k] = '\0';
	return rt_cat(dst, cap, out);
}

long rt_atol(const char *s)
{
	long v = 0;
	int neg = 0;

	if (!s)
		return 0;
	if (*s == '-')
	{
		neg = 1;
		s++;
	}
	while (*s >= '0' && *s <= '9')
		v = v * 10 + (*s++ - '0');
	return neg ? -v : v;
}

void rt_puts(const char *s)
{
	(void)S3(SYS_write, 1, s, rt_strlen(s));
}

void rt_putn(long v)
{
	char buf[26];

	buf[0] = '\0';
	rt_puts(rt_catn(buf, sizeof(buf), v));
}

void rt_putx(unsigned long v)
{
	char buf[19];
	int i;

	buf[0] = '0';
	buf[1] = 'x';
	for (i = 0; i < 16; i++)
		buf[2 + i] = "0123456789abcdef"[(v >> ((15 - i) * 4)) & 0xf];
	buf[18] = '\0';
	rt_puts(buf);
}

void rt_diag(const char *label, long value)
{
	char line[160];

	line[0] = '\0';
	rt_cat(line, sizeof(line), "# ");
	rt_cat(line, sizeof(line), label);
	rt_cat(line, sizeof(line), "=");
	rt_catn(line, sizeof(line), value);
	rt_cat(line, sizeof(line), "\n");
	rt_puts(line);
}

long rt_fork(void)
{
	/* clone(SIGCHLD, 0, ...): the generic fork(2) on both ISAs. */
	return S5(SYS_clone, SIGCHLD, 0, 0, 0, 0);
}

long rt_wait(long pid, int *status, int options)
{
	return S4(SYS_wait4, pid, status, options, 0);
}

void rt_exit(int code)
{
	for (;;)
		(void)S1(SYS_exit_group, code);
}

long rt_sigaction(int sig, void (*handler)(int), unsigned long flags,
		  struct ksigaction *old)
{
	struct ksigaction act;

	act.handler = handler;
	act.flags = flags | SA_RESTORER;
	act.restorer = rt_restore;
	act.mask = 0;
	return S4(SYS_rt_sigaction, sig, &act, old, sizeof(act.mask));
}

long rt_now_ms(void)
{
	struct timespec ts;

	if (S2(SYS_clock_gettime, CLOCK_MONOTONIC, &ts) != 0)
		return -1;
	return ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

void rt_sleep_ms(long ms)
{
	struct timespec ts;

	ts.tv_sec = ms / 1000L;
	ts.tv_nsec = (ms % 1000L) * 1000000L;
	(void)S2(SYS_nanosleep, &ts, 0);
}

/* Remove a case directory tree (bounded depth; best effort). */
void rt_rmtree(int dirfd, const char *name, int depth)
{
	char buf[1024];
	long fd;
	long n;

	if (S3(SYS_unlinkat, dirfd, name, 0) == 0 || depth > 4)
		return;
	fd = S4(SYS_openat, dirfd, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW, 0);
	if (fd < 0)
		return;
	while ((n = S3(SYS_getdents64, fd, buf, sizeof(buf))) > 0)
	{
		long off = 0;

		while (off < n)
		{
			struct dirent *d = (struct dirent *)(buf + off);

			if (d->d_reclen == 0)
				break;
			if (!rt_streq(d->d_name, ".") && !rt_streq(d->d_name, ".."))
				rt_rmtree((int)fd, d->d_name, depth + 1);
			off += d->d_reclen;
		}
	}
	(void)S1(SYS_close, fd);
	(void)S3(SYS_unlinkat, dirfd, name, AT_REMOVEDIR);
}
