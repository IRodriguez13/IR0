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
#define SYS_clone		220
#define SYS_wait4		260
#define SYS_mmap		222
#define SIGCHLD			17
#define SIGSEGV			11
#define PROT_READ		0x1
#define PROT_WRITE		0x2
#define MAP_PRIVATE		0x02
#define MAP_ANONYMOUS		0x20
#define MAP_FAILED		((void *)-1)
#define WIFSIGNALED(s)		(((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define WTERMSIG(s)		((s) & 0x7f)
#define WIFEXITED(s)		(((s) & 0x7f) == 0)
#define WEXITSTATUS(s)		(((s) >> 8) & 0xff)

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

static long nolibc_svc6(long nr, long a, long b, long c, long d, long e,
			long f)
{
	register long x8 __asm__("x8") = nr;
	register long x0 __asm__("x0") = a;
	register long x1 __asm__("x1") = b;
	register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d;
	register long x4 __asm__("x4") = e;
	register long x5 __asm__("x5") = f;

	__asm__ volatile("svc #0"
			 : "+r"(x0)
			 : "r"(x8), "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5)
			 : "memory", "cc");
	return x0;
}

static int fork(void)
{
	/* clone(SIGCHLD, 0, NULL, NULL, 0): the generic fork(2) encoding. */
	return (int)syscall(SYS_clone, SIGCHLD, 0, 0, 0);
}

static int wait4(int pid, int *status, int options, void *rusage)
{
	return (int)syscall(SYS_wait4, pid, (long)status, options,
			    (long)rusage);
}

static void _exit(int code)
{
	nolibc_svc(SYS_exit, code, 0, 0, 0);
	for (;;)
		;
}

static void *mmap(void *addr, size_t len, int prot, int flags, int fd,
		  long off)
{
	long ret = nolibc_svc6(SYS_mmap, (long)addr, (long)len, prot, flags,
			       fd, off);

	if (ret < 0 && ret > -4096)
	{
		errno = (int)(-ret);
		return MAP_FAILED;
	}
	return (void *)ret;
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
