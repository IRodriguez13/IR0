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
#define O_WRONLY		1
#define SYS_read		63
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
#define SYS_mprotect		226
#define SYS_rt_sigaction	134
#define SYS_rt_sigprocmask	135
#define SYS_kill		129
#define SYS_rt_sigreturn	139
#define SYS_socket		198
#define SYS_socketpair		199
#define SYS_sendto		206
#define SYS_recvfrom		207
#define SA_RESTORER		0x04000000UL
#define SIGCHLD			17
#define SIGILL			4
#define SIGTRAP			5
#define SIGKILL			9
#define SIGUSR1			10
#define SIGSEGV			11
#define SIGTERM			15
#define SIGABRT			6
#define SIG_BLOCK		0
#define SIG_UNBLOCK		1
#define PROT_READ		0x1
#define PROT_WRITE		0x2
#define MAP_PRIVATE		0x02
#define MAP_ANONYMOUS		0x20
#define MAP_FAILED		((void *)-1)
#define AF_UNIX			1
#define AF_INET			2
#define SOCK_STREAM		1
#define SOCK_DGRAM		2
#define MSG_DONTWAIT		0x40
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

static ssize_t read(int fd, void *buf, size_t count)
{
	return (ssize_t)syscall(SYS_read, fd, (long)buf, (long)count, 0);
}

static ssize_t write(int fd, const void *buf, size_t count)
{
	return (ssize_t)syscall(SYS_write, fd, (long)buf, (long)count, 0);
}

static int open(const char *path, int flags)
{
	return (int)syscall(SYS_openat, AT_FDCWD, (long)path, flags, 0);
}

static int close(int fd)
{
	return (int)syscall(SYS_close, fd);
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

/* Kernel rt_sigaction(2) layout (asm-generic). */
struct sigaction {
	void (*sa_handler)(int);
	unsigned long sa_flags;
	void (*sa_restorer)(void);
	unsigned long sa_mask;
};

void nolibc_restore_rt(void);

/* Like musl: every handler returns through an rt_sigreturn trampoline. */
__asm__(
	".global nolibc_restore_rt\n"
	".type nolibc_restore_rt, %function\n"
	"nolibc_restore_rt:\n"
	"	mov	x8, #139\n"
	"	svc	#0\n"
);

static int sigaction(int sig, const struct sigaction *act,
		     struct sigaction *old)
{
	struct sigaction ksa;

	if (!act)
		return (int)syscall(SYS_rt_sigaction, sig, 0, (long)old,
				    (long)sizeof(ksa.sa_mask));
	ksa = *act;
	ksa.sa_flags |= SA_RESTORER;
	ksa.sa_restorer = nolibc_restore_rt;
	return (int)syscall(SYS_rt_sigaction, sig, (long)&ksa, (long)old,
			    (long)sizeof(ksa.sa_mask));
}

typedef unsigned long sigset_t;

static int sigemptyset(sigset_t *set)
{
	*set = 0;
	return 0;
}

static int sigaddset(sigset_t *set, int sig)
{
	*set |= 1UL << (sig - 1);
	return 0;
}

static int sigismember(const sigset_t *set, int sig)
{
	return (*set >> (sig - 1)) & 1UL ? 1 : 0;
}

static int sigprocmask(int how, const sigset_t *set, sigset_t *old)
{
	return (int)syscall(SYS_rt_sigprocmask, how, (long)set, (long)old,
			    (long)sizeof(*set));
}

static int getpid(void)
{
	return (int)syscall(SYS_getpid);
}

static int kill(int pid, int sig)
{
	return (int)syscall(SYS_kill, pid, sig);
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

/* Six-argument syscalls (sendto, recvfrom) with the libc errno convention. */
static long nolibc_call6(long nr, long a, long b, long c, long d, long e,
			 long f)
{
	long ret = nolibc_svc6(nr, a, b, c, d, e, f);

	if (ret < 0 && ret > -4096)
	{
		errno = (int)(-ret);
		return -1;
	}
	return ret;
}

static int socket(int domain, int type, int protocol)
{
	return (int)syscall(SYS_socket, domain, type, protocol);
}

static int socketpair(int domain, int type, int protocol, int sv[2])
{
	return (int)syscall(SYS_socketpair, domain, type, protocol, (long)sv);
}

static ssize_t sendto(int fd, const void *buf, size_t len, int flags,
		      const void *addr, unsigned int addrlen)
{
	return (ssize_t)nolibc_call6(SYS_sendto, fd, (long)buf, (long)len,
				     flags, (long)addr, (long)addrlen);
}

static ssize_t recvfrom(int fd, void *buf, size_t len, int flags, void *addr,
			unsigned int *addrlen)
{
	return (ssize_t)nolibc_call6(SYS_recvfrom, fd, (long)buf, (long)len,
				     flags, (long)addr, (long)addrlen);
}

static ssize_t send(int fd, const void *buf, size_t len, int flags)
{
	return sendto(fd, buf, len, flags, 0, 0);
}

static ssize_t recv(int fd, void *buf, size_t len, int flags)
{
	return recvfrom(fd, buf, len, flags, 0, 0);
}

static int mprotect(void *addr, size_t len, int prot)
{
	return (int)syscall(SYS_mprotect, (long)addr, (long)len, prot);
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
