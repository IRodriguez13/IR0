/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: nolibc.h
 * Description: CRT-less syscall helpers for ARM64 selftests (in-image stack).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_SELFTEST_NOLIBC_H
#define IR0_SELFTEST_NOLIBC_H

#define AT_FDCWD		-100
#define O_RDONLY		0
#define SYS_write		64
#define SYS_close		57
#define SYS_exit		93
#define SYS_getpid		172
#define SYS_openat		56
#define SYS_swapon		224
#define SYS_swapoff		225

typedef long ssize_t;
typedef unsigned long size_t;

int errno;

static long nolibc_svc(long nr, long a, long b, long c, long d)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3)
			 : "memory", "cc");
	return x0;
}

static long nolibc_call(long nr, long a, long b, long c, long d, ...)
{
	long ret = nolibc_svc(nr, a, b, c, d);

	if (ret < 0)
	{
		errno = (int)(-ret);
		return -1;
	}
	return ret;
}

/* ## eats the comma when the selftest passes only the syscall number. */
#define syscall(nr, ...) nolibc_call((nr), ##__VA_ARGS__, 0L, 0L, 0L, 0L)

static ssize_t write(int fd, const void *buf, size_t count)
{
	return (ssize_t)syscall(SYS_write, fd, (long)buf, (long)count, 0);
}

unsigned char nolibc_stack[8192] __attribute__((aligned(16))) = { 1 };

int main(void);
void _start(void);

__asm__(
	".global _start\n"
	".type _start, %function\n"
	"_start:\n"
	"	adrp	x0, nolibc_stack\n"
	"	add	x0, x0, :lo12:nolibc_stack\n"
	"	add	sp, x0, #8192\n"
	"	bl	main\n"
	"	mov	x8, #93\n"
	"	svc	#0\n"
);

#endif /* IR0_SELFTEST_NOLIBC_H */
