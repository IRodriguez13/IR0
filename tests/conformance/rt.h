/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: rt.h
 * Description: Freestanding runtime shared by the conformance runner and the
 *              syscall fuzzer: raw syscalls that return -errno exactly as the
 *              kernel does (no libc wrapper in between), a signal trampoline,
 *              and bounded serial output. musl headers supply the per-ISA
 *              constants and structure layouts; no libc function is called,
 *              so the same source runs on x86_64 (musl crt) and arm64
 *              (no libc at all).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifndef IR0_CONFORMANCE_RT_H
#define IR0_CONFORMANCE_RT_H

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/syscall.h>
#include <sys/wait.h>

static inline long rsys(long nr, long a, long b, long c, long d, long e,
			long f)
{
#if defined(__x86_64__)
	register long r10 __asm__("r10") = d;
	register long r8 __asm__("r8") = e;
	register long r9 __asm__("r9") = f;
	long ret;

	__asm__ volatile("syscall"
			 : "=a"(ret)
			 : "a"(nr), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8),
			   "r"(r9)
			 : "rcx", "r11", "memory", "cc");
	return ret;
#elif defined(__aarch64__)
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
#else
#error "conformance runtime: unsupported ISA"
#endif
}

#define S0(n) rsys((n), 0, 0, 0, 0, 0, 0)
#define S1(n, a) rsys((n), (long)(a), 0, 0, 0, 0, 0)
#define S2(n, a, b) rsys((n), (long)(a), (long)(b), 0, 0, 0, 0)
#define S3(n, a, b, c) rsys((n), (long)(a), (long)(b), (long)(c), 0, 0, 0)
#define S4(n, a, b, c, d) \
	rsys((n), (long)(a), (long)(b), (long)(c), (long)(d), 0, 0)
#define S5(n, a, b, c, d, e) \
	rsys((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e), 0)
#define S6(n, a, b, c, d, e, f)                                              \
	rsys((n), (long)(a), (long)(b), (long)(c), (long)(d), (long)(e),      \
	     (long)(f))

/* Kernel rt_sigaction(2) argument: same field order on x86_64 and arm64. */
struct ksigaction
{
	void (*handler)(int);
	unsigned long flags;
	void (*restorer)(void);
	unsigned long mask;
};

#ifndef SA_RESTORER
#define SA_RESTORER 0x04000000UL
#endif

#define RT_PAGE 4096UL
#define SIGBIT(sig) (1UL << ((sig) - 1))

/*
 * Addresses user code must never reach through a syscall: the Linux kernel
 * half, and the supervisor-only pages IR0 maps into every process root
 * (x86_64 keyboard ring at 0x500000; arm64 identity-mapped heap).
 */
#if defined(__x86_64__)
#define RT_MACHINE "x86_64"
#define RT_KERNEL_ADDR_A 0xffff800000000000UL
#define RT_KERNEL_ADDR_B 0xffffffff80100000UL
#define RT_SUPERVISOR_ADDR 0x500000UL
#else
#define RT_MACHINE "aarch64"
#define RT_KERNEL_ADDR_A 0xffff000000000000UL
#define RT_KERNEL_ADDR_B 0xffffffffc0000000UL
#define RT_SUPERVISOR_ADDR 0x40800000UL
#endif
#define RT_NONCANONICAL 0x0000900000000000UL

/* Output on fd 1 (the serial console in the guest). */
void rt_puts(const char *s);
void rt_putn(long v);
void rt_putx(unsigned long v);
void rt_diag(const char *label, long value);

/* String/memory helpers (no libc on arm64). */
size_t rt_strlen(const char *s);
int rt_streq(const char *a, const char *b);
int rt_memeq(const void *a, const void *b, size_t n);
void rt_fill(void *p, int byte, size_t n);
void rt_copy(void *dst, const void *src, size_t n);
char *rt_cat(char *dst, size_t cap, const char *s);
char *rt_catn(char *dst, size_t cap, long v);
long rt_atol(const char *s);

/* Remove dirfd/name recursively (bounded depth; best effort). */
void rt_rmtree(int dirfd, const char *name, int depth);

/* Thin syscall helpers used everywhere. */
long rt_fork(void);
long rt_wait(long pid, int *status, int options);
void rt_exit(int code) __attribute__((noreturn));
long rt_sigaction(int sig, void (*handler)(int), unsigned long flags,
		  struct ksigaction *old);
long rt_now_ms(void);
void rt_sleep_ms(long ms);

/* Child status decoding (Linux wait status encoding). */
#define RT_EXITED(s) (((s) & 0x7f) == 0)
#define RT_EXITCODE(s) (((s) >> 8) & 0xff)
#define RT_SIGNALED(s) (((s) & 0x7f) != 0 && ((s) & 0x7f) != 0x7f)
#define RT_TERMSIG(s) ((s) & 0x7f)

/* Entry point both programs implement. */
int rt_main(int argc, char **argv);

#endif /* IR0_CONFORMANCE_RT_H */
