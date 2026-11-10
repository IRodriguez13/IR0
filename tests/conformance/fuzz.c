/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fuzz.c
 * Description: Bounded, deterministic syscall fuzzer (/sbin/init). Issues
 *              FUZZ_COUNT random-but-typed syscalls derived from FUZZ_SEED:
 *              bad pointers (NULL, kernel half, IR0 supervisor pages,
 *              non-canonical, read-only, straddling a hole), huge and
 *              negative lengths, negative and stale fds, and fuzzed flags.
 *              Calls run in batches; each batch is a forked child working
 *              in its own scratch directory, so a crash or hang of the
 *              child is contained. After every batch the parent (init)
 *              checks kernel health: init is alive, the child was reaped,
 *              and canary syscalls (pipe round trip, mmap, clock, fork)
 *              still work. Batch b of seed s is always the same sequence,
 *              so FUZZ_BATCH=b FUZZ_TRACE=1 replays one batch call by call.
 *
 *              Oracles besides health: an unassigned syscall number must
 *              return -ENOSYS, and no syscall may leak an internal error
 *              code (-4095..-134, e.g. ERESTARTSYS) to userspace.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "rt.h"

#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <time.h>

#ifndef FUZZ_SEED
#define FUZZ_SEED 1UL
#endif
#ifndef FUZZ_COUNT
#define FUZZ_COUNT 2000L
#endif
#ifndef FUZZ_BATCH
#define FUZZ_BATCH 0L /* 0: all batches; b: only batch b (replay) */
#endif
#ifndef FUZZ_TRACE
#define FUZZ_TRACE 0
#endif
#ifndef FUZZ_MAX_SEC
#define FUZZ_MAX_SEC 120L
#endif

#define BATCH_CALLS 100L
#define BATCH_TIMEOUT_MS 4000L
#define POOL_MAX 24
#define ARENA_PAGES 16
#define BUF_PAGES 4
#define ERRNO_MAX_LINUX 133 /* EHWPOISON */
#define UNKNOWN_NR_LO 512
#define UNKNOWN_NR_HI 1000

/*
 * Progress record the child sends after every call through a pipe (not a
 * MAP_SHARED page: the report must not depend on the memory model under
 * test). The parent keeps the last complete record.
 */
struct fuzz_shared
{
	long calls;
	long leaked_errno;
	long unknown_not_enosys;
	long last_index;
	long last_nr;
	long last_args[6];
	long last_ret;
	long first_bad_nr;
	long first_bad_ret;
};

/* Argument kinds. */
enum
{
	A_NONE,
	A_FD,
	A_PTR,
	A_LEN,
	A_FLAGS,
	A_PATH,
	A_INT,
	A_OFF,
	A_ADDR,
	A_SIG,
	A_CLOCK,
	A_IOV,
	A_PID,
	A_MODE,
	A_SOCKADDR,
	A_TS,
	A_SIGACT,
	A_SIGSET,
	A_NAME,
};

struct fuzz_call
{
	long nr;
	unsigned char arg[6];
	unsigned char weight;
};

#define C(nr, a, b, c, d, e, f, w) { (nr), { a, b, c, d, e, f }, (w) }
#define N A_NONE

static const struct fuzz_call g_calls[] = {
	C(SYS_read, A_FD, A_PTR, A_LEN, N, N, N, 6),
	C(SYS_write, A_FD, A_PTR, A_LEN, N, N, N, 6),
	C(SYS_pread64, A_FD, A_PTR, A_LEN, A_OFF, N, N, 3),
	C(SYS_pwrite64, A_FD, A_PTR, A_LEN, A_OFF, N, N, 3),
	C(SYS_readv, A_FD, A_IOV, A_INT, N, N, N, 3),
	C(SYS_writev, A_FD, A_IOV, A_INT, N, N, N, 3),
	C(SYS_openat, A_FD, A_PATH, A_FLAGS, A_MODE, N, N, 6),
	C(SYS_close, A_FD, N, N, N, N, N, 3),
	C(SYS_fstat, A_FD, A_PTR, N, N, N, N, 3),
	C(SYS_newfstatat, A_FD, A_PATH, A_PTR, A_FLAGS, N, N, 3),
	C(SYS_statx, A_FD, A_PATH, A_FLAGS, A_FLAGS, A_PTR, N, 2),
	C(SYS_lseek, A_FD, A_OFF, A_INT, N, N, N, 3),
	C(SYS_ftruncate, A_FD, A_OFF, N, N, N, N, 2),
	C(SYS_getdents64, A_FD, A_PTR, A_LEN, N, N, N, 3),
	C(SYS_dup, A_FD, N, N, N, N, N, 2),
	C(SYS_dup3, A_FD, A_FD, A_FLAGS, N, N, N, 2),
	C(SYS_fcntl, A_FD, A_INT, A_INT, N, N, N, 4),
	C(SYS_ioctl, A_FD, A_INT, A_PTR, N, N, N, 4),
	C(SYS_pipe2, A_PTR, A_FLAGS, N, N, N, N, 2),
	C(SYS_mkdirat, A_FD, A_NAME, A_MODE, N, N, N, 2),
	C(SYS_unlinkat, A_FD, A_NAME, A_FLAGS, N, N, N, 2),
	C(SYS_renameat, A_FD, A_NAME, A_FD, A_NAME, N, N, 2),
	C(SYS_symlinkat, A_NAME, A_FD, A_NAME, N, N, N, 1),
	C(SYS_linkat, A_FD, A_NAME, A_FD, A_NAME, A_FLAGS, N, 1),
	C(SYS_readlinkat, A_FD, A_PATH, A_PTR, A_LEN, N, N, 2),
	C(SYS_faccessat, A_FD, A_PATH, A_INT, N, N, N, 2),
	C(SYS_fchmodat, A_FD, A_NAME, A_MODE, N, N, N, 1),
	C(SYS_getcwd, A_PTR, A_LEN, N, N, N, N, 2),
	C(SYS_chdir, A_NAME, N, N, N, N, N, 1),
	C(SYS_fchdir, A_FD, N, N, N, N, N, 1),
	C(SYS_mmap, A_ADDR, A_LEN, A_INT, A_FLAGS, A_FD, A_OFF, 6),
	C(SYS_munmap, A_ADDR, A_LEN, N, N, N, N, 4),
	C(SYS_mprotect, A_ADDR, A_LEN, A_INT, N, N, N, 4),
	C(SYS_madvise, A_ADDR, A_LEN, A_INT, N, N, N, 2),
	C(SYS_msync, A_ADDR, A_LEN, A_INT, N, N, N, 1),
	C(SYS_mremap, A_ADDR, A_LEN, A_LEN, A_FLAGS, A_ADDR, N, 2),
	C(SYS_brk, A_ADDR, N, N, N, N, N, 2),
	C(SYS_clock_gettime, A_CLOCK, A_PTR, N, N, N, N, 2),
	C(SYS_clock_getres, A_CLOCK, A_PTR, N, N, N, N, 1),
	C(SYS_gettimeofday, A_PTR, A_PTR, N, N, N, N, 1),
	C(SYS_nanosleep, A_TS, A_PTR, N, N, N, N, 2),
	C(SYS_rt_sigaction, A_SIG, A_SIGACT, A_PTR, A_LEN, N, N, 3),
	C(SYS_rt_sigprocmask, A_INT, A_SIGSET, A_PTR, A_LEN, N, N, 3),
	C(SYS_rt_sigpending, A_PTR, A_LEN, N, N, N, N, 1),
	C(SYS_sigaltstack, A_PTR, A_PTR, N, N, N, N, 1),
	C(SYS_kill, A_PID, A_SIG, N, N, N, N, 2),
	C(SYS_rt_sigreturn, N, N, N, N, N, N, 1),
	C(SYS_wait4, A_PID, A_PTR, A_FLAGS, A_PTR, N, N, 2),
	C(SYS_getpid, N, N, N, N, N, N, 1),
	C(SYS_getppid, N, N, N, N, N, N, 1),
	C(SYS_uname, A_PTR, N, N, N, N, N, 1),
	C(SYS_umask, A_MODE, N, N, N, N, N, 1),
	C(SYS_sched_yield, N, N, N, N, N, N, 1),
	C(SYS_getrandom, A_PTR, A_LEN, A_FLAGS, N, N, N, 1),
	C(SYS_ppoll, A_PTR, A_INT, A_TS, A_SIGSET, A_LEN, N, 2),
	C(SYS_socket, A_INT, A_FLAGS, A_INT, N, N, N, 3),
	C(SYS_socketpair, A_INT, A_FLAGS, A_INT, A_PTR, N, N, 2),
	C(SYS_bind, A_FD, A_SOCKADDR, A_LEN, N, N, N, 2),
	C(SYS_connect, A_FD, A_SOCKADDR, A_LEN, N, N, N, 2),
	C(SYS_listen, A_FD, A_INT, N, N, N, N, 1),
	C(SYS_accept4, A_FD, A_PTR, A_PTR, A_FLAGS, N, N, 1),
	C(SYS_sendto, A_FD, A_PTR, A_LEN, A_FLAGS, A_SOCKADDR, A_LEN, 3),
	C(SYS_recvfrom, A_FD, A_PTR, A_LEN, A_FLAGS, A_PTR, A_PTR, 3),
	C(SYS_getsockname, A_FD, A_PTR, A_PTR, N, N, N, 1),
	C(SYS_getpeername, A_FD, A_PTR, A_PTR, N, N, N, 1),
	C(SYS_setsockopt, A_FD, A_INT, A_INT, A_PTR, A_LEN, N, 2),
	C(SYS_getsockopt, A_FD, A_INT, A_INT, A_PTR, A_PTR, N, 2),
	C(SYS_shutdown, A_FD, A_INT, N, N, N, N, 1),
	C(-1, N, N, N, N, N, N, 2), /* unassigned syscall number */
};
#define NCALLS ((long)(sizeof(g_calls) / sizeof(g_calls[0])))

static unsigned long g_rng;
static long g_pool[POOL_MAX];
static int g_npool;
static unsigned char *g_buf;   /* BUF_PAGES RW, then an unmapped hole */
static unsigned char *g_ro;    /* one read-only page */
static unsigned char *g_arena; /* ARENA_PAGES the fuzzer may remap */
static struct fuzz_shared g_rec;
static struct fuzz_shared *g_sh = &g_rec;
static int g_report_fd = -1;
static long g_self;
static char g_longpath[4200];
static char g_base[96];

static unsigned long rnd(void)
{
	/* splitmix64: one state, fully determined by (seed, batch). */
	unsigned long z = (g_rng += 0x9e3779b97f4a7c15UL);

	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9UL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebUL;
	return z ^ (z >> 31);
}

static unsigned long pick(unsigned long n)
{
	return n ? rnd() % n : 0;
}

static void on_signal(int sig)
{
	/* Synchronous faults must not loop back into the faulting call. */
	if (sig == SIGSEGV || sig == SIGBUS || sig == SIGILL || sig == SIGFPE ||
	    sig == SIGTRAP)
		rt_exit(77);
}

static void pool_add(long fd)
{
	if (fd < 3)
		return;
	(void)S3(SYS_fcntl, fd, F_SETFL, O_NONBLOCK);
	if (g_npool < POOL_MAX)
		g_pool[g_npool++] = fd;
	else
		(void)S1(SYS_close, fd);
}

static long gen_fd(void)
{
	static const long bad[] = { -1, -2, -100, (long)0x80000000L,
				    0x7fffffff, 1024, 65535, 4096 };

	if (g_npool && pick(10) < 7)
		return g_pool[pick((unsigned long)g_npool)];
	if (pick(4) == 0)
		return AT_FDCWD;
	return bad[pick(sizeof(bad) / sizeof(bad[0]))];
}

static long gen_ptr(void)
{
	switch (pick(14))
	{
	case 0:
		return 0;
	case 1:
		return 1;
	case 2:
		return (long)RT_KERNEL_ADDR_A;
	case 3:
		return (long)RT_KERNEL_ADDR_B;
	case 4:
		return (long)RT_SUPERVISOR_ADDR;
	case 5:
		return (long)RT_NONCANONICAL;
	case 6:
		return -1;
	case 7:
		return (long)g_ro;
	case 8:
		/* Straddles the end of the buffer into the unmapped hole. */
		return (long)(g_buf + BUF_PAGES * RT_PAGE - 1 - pick(64));
	case 9:
		return (long)(g_buf + BUF_PAGES * RT_PAGE);
	case 10:
		return (long)(g_arena + pick(ARENA_PAGES) * RT_PAGE);
	default:
		return (long)(g_buf + pick(BUF_PAGES * RT_PAGE / 2));
	}
}

static long gen_len(void)
{
	static const long lens[] = { 0, 1, 2, 7, 8, 16, 64, 128, 4095, 4096,
				     4097, 8192, 65536, 1L << 20, 1L << 30,
				     1L << 40, 0x7fffffffL, 0xffffffffL, -1,
				     -4096, (long)0x8000000000000000UL };

	if (pick(4) == 0)
		return (long)(rnd() & 0x3fff);
	return lens[pick(sizeof(lens) / sizeof(lens[0]))];
}

static long gen_flags(void)
{
	switch (pick(4))
	{
	case 0:
		return (long)(unsigned int)rnd();
	case 1:
		return 0;
	default:
		return (long)(rnd() & (rnd() & 0x1fffffUL));
	}
}

static long gen_int(void)
{
	static const long ints[] = { 0, 1, 2, 3, 4, 5, 8, 16, 1024, -1, -2,
				     0x7fffffff, (long)0x80000000L, 0x5401,
				     0x541b, 0x8912 };

	if (pick(3) == 0)
		return (long)(int)rnd();
	return ints[pick(sizeof(ints) / sizeof(ints[0]))];
}

static long gen_off(void)
{
	static const long offs[] = { 0, 1, 4096, 100, -1, -4096, 1L << 31,
				     1L << 40, 0x7fffffffffffffffL,
				     (long)0x8000000000000000UL };

	return offs[pick(sizeof(offs) / sizeof(offs[0]))];
}

/* Relative names only: everything the fuzzer creates stays in its dir. */
static long gen_name(void)
{
	static const char *const names[] = { "f0", "f1", "d0", "d0/f2", "l0",
					     "nope", "", "nope/x", "f0/x", "." };

	if (pick(10) == 0)
		return (long)g_longpath;
	if (pick(12) == 0)
		return gen_ptr();
	return (long)names[pick(sizeof(names) / sizeof(names[0]))];
}

/* Paths that may also name read-only system files. */
static long gen_path(void)
{
	static const char *const ro[] = { "/proc/uptime", "/proc/self/status",
					  "/dev/null", "/nonexistent/x" };

	if (pick(4) == 0)
		return (long)ro[pick(sizeof(ro) / sizeof(ro[0]))];
	return gen_name();
}

static long gen_addr(void)
{
	switch (pick(8))
	{
	case 0:
		return 0;
	case 1:
		return (long)RT_KERNEL_ADDR_A;
	case 2:
		return (long)RT_SUPERVISOR_ADDR;
	case 3:
		return (long)RT_NONCANONICAL;
	case 4:
		return (long)(g_arena + pick(ARENA_PAGES * RT_PAGE));
	default:
		return (long)(g_arena + pick(ARENA_PAGES) * RT_PAGE);
	}
}

static long gen_sig(void)
{
	long sig = (long)pick(70);

	/* Stopping ourselves only turns into a hang, as on Linux. */
	if (sig == SIGSTOP || sig == SIGTSTP || sig == SIGTTIN ||
	    sig == SIGTTOU || (sig == SIGKILL && pick(8)))
		return SIGUSR1;
	return sig;
}

static long gen_iov(void)
{
	struct iovec *iov = (struct iovec *)(g_buf + 2 * RT_PAGE);
	int i;

	if (pick(6) == 0)
		return gen_ptr();
	for (i = 0; i < 8; i++)
	{
		iov[i].iov_base = (void *)gen_ptr();
		iov[i].iov_len = (size_t)(pick(3) ? pick(256) : (unsigned long)gen_len());
	}
	return (long)iov;
}

static long gen_pid(void)
{
	static const long pids[] = { 0x3ffffff0L, -0x3ffffff0L, -1, 0 };

	if (pick(2) == 0)
		return g_self;
	return pids[pick(sizeof(pids) / sizeof(pids[0]))];
}

static long gen_sockaddr(void)
{
	unsigned char *a = g_buf + 3 * RT_PAGE;
	unsigned int i;

	if (pick(4) == 0)
		return gen_ptr();
	for (i = 0; i < 128; i++)
		a[i] = (unsigned char)rnd();
	a[0] = (unsigned char)(pick(3) ? AF_UNIX : pick(48));
	a[1] = 0;
	if (a[0] == AF_UNIX)
	{
		a[2] = (unsigned char)('a' + pick(3));
		a[3] = pick(2) ? 0 : (unsigned char)('0' + pick(3));
		a[4] = 0;
	}
	return (long)a;
}

static long gen_ts(void)
{
	struct timespec *ts = (struct timespec *)(g_buf + 3 * RT_PAGE + 512);
	static const long ns[] = { 0, 1, 1000000, 999999999, 1000000000, -1 };

	if (pick(5) == 0)
		return gen_ptr();
	ts->tv_sec = pick(8) ? 0 : -1;
	ts->tv_nsec = ns[pick(sizeof(ns) / sizeof(ns[0]))];
	return (long)ts;
}

static long gen_sigact(void)
{
	struct ksigaction *act = (struct ksigaction *)(g_buf + 3 * RT_PAGE + 1024);
	extern void rt_restore(void);
	static void (*const handlers[])(int) = { SIG_DFL, SIG_IGN, on_signal,
						 (void (*)(int))0x1000 };

	if (pick(5) == 0)
		return gen_ptr();
	act->handler = handlers[pick(4)];
	act->flags = (unsigned long)gen_flags() | SA_RESTORER;
	act->restorer = pick(8) ? rt_restore : (void (*)(void))gen_ptr();
	act->mask = rnd();
	return (long)act;
}

static long gen_sigset(void)
{
	unsigned long *set = (unsigned long *)(g_buf + 3 * RT_PAGE + 1536);

	if (pick(5) == 0)
		return gen_ptr();
	set[0] = rnd();
	set[1] = rnd();
	return (long)set;
}

static long gen_arg(unsigned char kind)
{
	switch (kind)
	{
	case A_FD:
		return gen_fd();
	case A_PTR:
		return gen_ptr();
	case A_LEN:
		return gen_len();
	case A_FLAGS:
		return gen_flags();
	case A_PATH:
		return gen_path();
	case A_INT:
		return gen_int();
	case A_OFF:
		return gen_off();
	case A_ADDR:
		return gen_addr();
	case A_SIG:
		return gen_sig();
	case A_CLOCK:
		return pick(2) ? (long)pick(12) : gen_int();
	case A_IOV:
		return gen_iov();
	case A_PID:
		return gen_pid();
	case A_MODE:
		return (long)(rnd() & 07777);
	case A_SOCKADDR:
		return gen_sockaddr();
	case A_TS:
		return gen_ts();
	case A_SIGACT:
		return gen_sigact();
	case A_SIGSET:
		return gen_sigset();
	case A_NAME:
		return gen_name();
	default:
		return 0;
	}
}

/* Keep the child from blocking forever or destroying its own harness. */
static void sanitize(long nr, long *a)
{
	int j;

	/* Never let a call close, replace or reconfigure the report pipe. */
	for (j = 0; j < 6; j++)
		if (g_report_fd >= 0 && a[j] == g_report_fd)
			a[j] = -1;
	if (nr == SYS_dup3 && a[1] >= 0 && a[1] < 3)
		a[1] = 100;
	if (nr == SYS_close && a[0] >= 0 && a[0] < 3)
		a[0] = -1;
	if (nr == SYS_fcntl && a[1] == F_SETFL)
		a[2] |= O_NONBLOCK;
	if (nr == SYS_fcntl && (a[1] == F_SETLKW || a[1] == F_OFD_SETLKW))
		a[1] = F_GETFD;
	if (nr == SYS_pipe2)
		a[1] |= O_NONBLOCK;
	if (nr == SYS_socket || nr == SYS_socketpair)
		a[1] |= SOCK_NONBLOCK;
	if (nr == SYS_accept4)
		a[3] |= SOCK_NONBLOCK;
	if (nr == SYS_sendto || nr == SYS_recvfrom)
		a[3] |= MSG_DONTWAIT;
	if (nr == SYS_wait4)
		a[2] |= WNOHANG;
	if (nr == SYS_openat)
		a[2] |= O_NONBLOCK;
	if (nr == SYS_ppoll)
	{
		/* A NULL timeout waits forever: always pass a zero one. */
		struct timespec *ts = (struct timespec *)(g_buf + 3 * RT_PAGE + 768);

		ts->tv_sec = 0;
		ts->tv_nsec = 0;
		if (pick(6))
			a[2] = (long)ts;
		else if (a[2] == 0)
			a[2] = (long)ts;
	}
	if (nr == SYS_nanosleep && a[0] == 0)
		a[0] = 1;
	if (nr == SYS_munmap || nr == SYS_mprotect || nr == SYS_madvise ||
	    nr == SYS_msync || nr == SYS_mremap)
	{
		/* Inside the arena: clamp the length so the program survives. */
		unsigned long lo = (unsigned long)g_arena;
		unsigned long hi = lo + ARENA_PAGES * RT_PAGE;

		if ((unsigned long)a[0] >= lo && (unsigned long)a[0] < hi &&
		    ((unsigned long)a[1] > hi - (unsigned long)a[0]))
			a[1] = (long)(hi - (unsigned long)a[0]);
		if (a[0] == 0 && a[1] != 0)
			a[1] = (long)RT_PAGE;
	}
	if (nr == SYS_mmap && (a[3] & MAP_FIXED))
	{
		unsigned long lo = (unsigned long)g_arena;
		unsigned long hi = lo + ARENA_PAGES * RT_PAGE;

		if ((unsigned long)a[0] >= lo && (unsigned long)a[0] < hi &&
		    (unsigned long)a[1] > hi - (unsigned long)a[0])
			a[1] = (long)(hi - (unsigned long)a[0]);
		if (a[0] == 0)
			a[3] &= ~(long)MAP_FIXED;
	}
	if (nr == SYS_mremap && (a[3] & 2 /* MREMAP_FIXED */))
		a[4] = (long)(g_arena + pick(ARENA_PAGES) * RT_PAGE);
}

static void trace_call(long i, long nr, const long *a, long ret)
{
	int k;

	rt_puts("# call ");
	rt_putn(i);
	rt_puts(" nr=");
	rt_putn(nr);
	for (k = 0; k < 6; k++)
	{
		rt_puts(" ");
		rt_putx((unsigned long)a[k]);
	}
	rt_puts(" ret=");
	rt_putn(ret);
	rt_puts("\n");
}

static void child_setup(long batch)
{
	char dir[128];
	int sv[2];
	long fd;

	dir[0] = '\0';
	rt_cat(dir, sizeof(dir), g_base);
	rt_cat(dir, sizeof(dir), "/b");
	rt_catn(dir, sizeof(dir), batch);
	(void)S3(SYS_mkdirat, AT_FDCWD, dir, 0755);
	if (S1(SYS_chdir, dir) != 0)
	{
		rt_diag("fuzz_batch_dir_errno", -S3(SYS_mkdirat, AT_FDCWD, dir, 0755));
		rt_exit(90);
	}
	g_self = S0(SYS_getpid);
	/* Own process group: kill(0, sig) reaches the batch, not init. */
	(void)S0(SYS_setsid);
	g_npool = 0;
	fd = S4(SYS_openat, AT_FDCWD, "f0", O_CREAT | O_RDWR, 0644);
	(void)S3(SYS_write, fd, "fuzz-data", 9);
	pool_add(fd);
	pool_add(S4(SYS_openat, AT_FDCWD, "f1", O_CREAT | O_RDWR | O_APPEND,
		    0600));
	(void)S3(SYS_mkdirat, AT_FDCWD, "d0", 0755);
	pool_add(S4(SYS_openat, AT_FDCWD, "d0", O_RDONLY | O_DIRECTORY, 0));
	pool_add(S4(SYS_openat, AT_FDCWD, ".", O_RDONLY | O_DIRECTORY, 0));
	pool_add(S4(SYS_openat, AT_FDCWD, "/proc/uptime", O_RDONLY, 0));
	if (S2(SYS_pipe2, sv, O_NONBLOCK) == 0)
	{
		pool_add(sv[0]);
		pool_add(sv[1]);
	}
	if (S4(SYS_socketpair, AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK, 0, sv) == 0)
	{
		pool_add(sv[0]);
		pool_add(sv[1]);
	}
	if (S4(SYS_socketpair, AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0, sv) == 0)
	{
		pool_add(sv[0]);
		pool_add(sv[1]);
	}
	pool_add(S3(SYS_socket, AF_INET, SOCK_DGRAM | SOCK_NONBLOCK, 0));
	(void)rt_sigaction(SIGSEGV, on_signal, 0, 0);
	(void)rt_sigaction(SIGBUS, on_signal, 0, 0);
	(void)rt_sigaction(SIGPIPE, SIG_IGN, 0, 0);
}

static void child_run(long batch, long ncalls)
{
	long weights = 0;
	long i;
	long k;

	g_rng = FUZZ_SEED * 0x2545f4914f6cdd1dUL + (unsigned long)batch;
	(void)rnd();
	child_setup(batch);
	for (k = 0; k < NCALLS; k++)
		weights += g_calls[k].weight;
	for (i = 0; i < ncalls; i++)
	{
		const struct fuzz_call *c = g_calls;
		long w = (long)pick((unsigned long)weights);
		long a[6];
		long nr;
		long ret;
		int j;

		for (k = 0; k < NCALLS; k++)
		{
			w -= g_calls[k].weight;
			if (w < 0)
			{
				c = &g_calls[k];
				break;
			}
		}
		nr = c->nr >= 0 ? c->nr
				: UNKNOWN_NR_LO +
					  (long)pick(UNKNOWN_NR_HI - UNKNOWN_NR_LO);
		for (j = 0; j < 6; j++)
			a[j] = c->nr >= 0 ? gen_arg(c->arg[j]) : (long)rnd();
		sanitize(nr, a);
		g_sh->last_index = i;
		g_sh->last_nr = nr;
		for (j = 0; j < 6; j++)
			g_sh->last_args[j] = a[j];
		(void)S3(SYS_write, g_report_fd, g_sh, sizeof(*g_sh));
		if (FUZZ_TRACE)
			trace_call(i, nr, a, 0x7eadbeef);
		ret = rsys(nr, a[0], a[1], a[2], a[3], a[4], a[5]);
		g_sh->calls++;
		g_sh->last_ret = ret;
		(void)S3(SYS_write, g_report_fd, g_sh, sizeof(*g_sh));
		if (FUZZ_TRACE)
			trace_call(i, nr, a, ret);
		if (nr == SYS_mmap)
			continue; /* returns addresses, never internal codes */
		if (ret < -ERRNO_MAX_LINUX && ret >= -4095)
		{
			if (!g_sh->leaked_errno++)
			{
				g_sh->first_bad_nr = nr;
				g_sh->first_bad_ret = ret;
			}
		}
		if (c->nr < 0 && ret != -ENOSYS)
		{
			if (!g_sh->unknown_not_enosys++ && !g_sh->leaked_errno)
			{
				g_sh->first_bad_nr = nr;
				g_sh->first_bad_ret = ret;
			}
		}
		if ((nr == SYS_openat || nr == SYS_dup || nr == SYS_socket ||
		     nr == SYS_accept4) &&
		    ret >= 3)
			pool_add(ret);
		if (nr == SYS_fcntl && (a[1] == F_DUPFD || a[1] == F_DUPFD_CLOEXEC) &&
		    ret >= 3)
			pool_add(ret);
	}
	rt_exit(0);
}

/* Canary checks: the kernel still does basic work for init. */
static int health_ok(long init_pid, const char **why)
{
	struct timespec a;
	struct timespec b;
	char buf[8];
	int p[2];
	int status = 0;
	long m;
	long pid;

	*why = "getpid";
	if (S0(SYS_getpid) != init_pid)
		return 0;
	*why = "pipe";
	if (S2(SYS_pipe2, p, 0) != 0)
		return 0;
	if (S3(SYS_write, p[1], "canary!", 8) != 8 ||
	    S3(SYS_read, p[0], buf, 8) != 8 || !rt_memeq(buf, "canary!", 8))
		return 0;
	(void)S1(SYS_close, p[0]);
	(void)S1(SYS_close, p[1]);
	*why = "mmap";
	m = S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE,
	       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (m < 0 && m > -4096)
		return 0;
	*(volatile char *)m = 1;
	if (S2(SYS_munmap, m, RT_PAGE) != 0)
		return 0;
	*why = "clock";
	if (S2(SYS_clock_gettime, CLOCK_MONOTONIC, &a) != 0)
		return 0;
	rt_sleep_ms(1);
	if (S2(SYS_clock_gettime, CLOCK_MONOTONIC, &b) != 0 ||
	    (b.tv_sec == a.tv_sec && b.tv_nsec <= a.tv_nsec))
		return 0;
	*why = "fork";
	pid = rt_fork();
	if (pid == 0)
		rt_exit(42);
	if (pid < 0)
		return 0;
	if (rt_wait(pid, &status, 0) != pid || !RT_EXITED(status) ||
	    RT_EXITCODE(status) != 42)
		return 0;
	return 1;
}

static void batch_cleanup(long batch)
{
	char dir[128];

	dir[0] = '\0';
	rt_cat(dir, sizeof(dir), g_base);
	rt_cat(dir, sizeof(dir), "/b");
	rt_catn(dir, sizeof(dir), batch);
	rt_rmtree(AT_FDCWD, dir, 0);
}

static void report_last(void)
{
	int k;

	rt_puts("# last call index=");
	rt_putn(g_sh->last_index);
	rt_puts(" nr=");
	rt_putn(g_sh->last_nr);
	for (k = 0; k < 6; k++)
	{
		rt_puts(" ");
		rt_putx((unsigned long)g_sh->last_args[k]);
	}
	rt_puts("\n");
}

/* Keep the newest complete record the child sent. */
static void drain(int fd)
{
	static struct fuzz_shared rec[16];
	long n;

	while ((n = S3(SYS_read, fd, rec, sizeof(rec))) > 0)
	{
		if (n >= (long)sizeof(rec[0]))
			rt_copy(&g_rec, &rec[n / (long)sizeof(rec[0]) - 1],
				sizeof(g_rec));
	}
}

/* Bounded wait; -ETIMEDOUT after a SIGKILL that the kernel honoured. */
static long wait_batch(long pid, int *status, int rfd)
{
	long start = rt_now_ms();
	long ret;

	for (;;)
	{
		drain(rfd);
		ret = rt_wait(pid, status, WNOHANG);
		if (ret != 0)
		{
			drain(rfd);
			return ret;
		}
		if (rt_now_ms() - start > BATCH_TIMEOUT_MS)
			break;
		rt_sleep_ms(2);
	}
	(void)S2(SYS_kill, pid, SIGKILL);
	start = rt_now_ms();
	for (;;)
	{
		drain(rfd);
		ret = rt_wait(pid, status, WNOHANG);
		if (ret == pid)
			return -ETIMEDOUT;
		if (ret < 0 || rt_now_ms() - start > BATCH_TIMEOUT_MS)
			return -EDEADLK; /* SIGKILL did not reap the child */
		rt_sleep_ms(2);
	}
}

int rt_main(int argc, char **argv)
{
	long init_pid = S0(SYS_getpid);
	long batches = (FUZZ_COUNT + BATCH_CALLS - 1) / BATCH_CALLS;
	long first = FUZZ_BATCH ? FUZZ_BATCH : 1;
	long last = FUZZ_BATCH ? FUZZ_BATCH : batches;
	long start = rt_now_ms();
	long calls = 0;
	long crashes = 0;
	long hangs = 0;
	long leaked = 0;
	long unknown = 0;
	long done = 0;
	long b;
	long i;
	int rp[2];

	g_base[0] = '\0';
	rt_cat(g_base, sizeof(g_base),
	       argc > 1 && argv[1][0] == '/' ? argv[1] : "/tmp");
	rt_cat(g_base, sizeof(g_base), "/ir0fuzz.");
	rt_catn(g_base, sizeof(g_base), init_pid);
	(void)S3(SYS_mkdirat, AT_FDCWD, g_base, 0755);
	for (i = 0; i < (long)sizeof(g_longpath) - 1; i++)
		g_longpath[i] = (i % 100 == 99) ? '/' : 'a';
	g_longpath[sizeof(g_longpath) - 1] = '\0';

	g_buf = (unsigned char *)S6(SYS_mmap, 0, (BUF_PAGES + 1) * RT_PAGE,
				    PROT_READ | PROT_WRITE,
				    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	g_ro = (unsigned char *)S6(SYS_mmap, 0, RT_PAGE, PROT_READ,
				   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	g_arena = (unsigned char *)S6(SYS_mmap, 0, ARENA_PAGES * RT_PAGE,
				      PROT_READ | PROT_WRITE,
				      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (S2(SYS_pipe2, rp, O_CLOEXEC) != 0 || (long)g_buf < 0 || (long)g_ro < 0 ||
	    (long)g_arena < 0)
	{
		rt_puts("# fuzz setup: mmap refused\nIR0_FUZZ_SETUP_BROKEN\n");
		for (;;)
			rt_sleep_ms(1000);
	}
	(void)S3(SYS_fcntl, rp[0], F_SETFL, O_NONBLOCK);
	(void)S2(SYS_munmap, g_buf + BUF_PAGES * RT_PAGE, RT_PAGE);

	rt_puts("# IR0 fuzz machine=" RT_MACHINE " seed=");
	rt_putn((long)FUZZ_SEED);
	rt_puts(" count=");
	rt_putn(FUZZ_COUNT);
	rt_puts(" batches=");
	rt_putn(last - first + 1);
	rt_puts("\n");

	for (b = first; b <= last; b++)
	{
		long n = FUZZ_COUNT - (b - 1) * BATCH_CALLS;
		const char *why = "";
		int status = 0;
		long pid;
		long ret;

		if (n > BATCH_CALLS)
			n = BATCH_CALLS;
		if (rt_now_ms() - start > FUZZ_MAX_SEC * 1000L)
		{
			rt_puts("# fuzz time budget reached\n");
			break;
		}
		rt_fill(g_sh, 0, sizeof(*g_sh));
		pid = rt_fork();
		if (pid == 0)
		{
			(void)S1(SYS_close, rp[0]);
			g_report_fd = rp[1];
			child_run(b, n);
		}
		if (pid < 0)
		{
			rt_diag("fuzz_fork_errno", -pid);
			break;
		}
		ret = wait_batch(pid, &status, rp[0]);
		calls += g_sh->calls;
		leaked += g_sh->leaked_errno;
		unknown += g_sh->unknown_not_enosys;
		rt_puts("# batch ");
		rt_putn(b);
		rt_puts(" calls=");
		rt_putn(g_sh->calls);
		if (ret == -ETIMEDOUT)
		{
			hangs++;
			rt_puts(" child=hung\n");
			report_last();
		}
		else if (ret != pid)
		{
			rt_puts(" child=unreapable\n");
			report_last();
			rt_puts("IR0_FUZZ_HEALTH_LOST reason=unkillable_child batch=");
			rt_putn(b);
			rt_puts("\n");
			for (;;)
				rt_sleep_ms(1000);
		}
		else if (RT_SIGNALED(status))
		{
			crashes++;
			rt_puts(" child=signal:");
			rt_putn(RT_TERMSIG(status));
			rt_puts("\n");
			report_last();
		}
		else
		{
			rt_puts(" child=exit:");
			rt_putn(RT_EXITCODE(status));
			rt_puts("\n");
			/* 77: a synchronous fault reached the batch's handler. */
			if (RT_EXITCODE(status) == 77)
				report_last();
		}
		batch_cleanup(b);
		if (g_sh->leaked_errno || g_sh->unknown_not_enosys)
		{
			rt_puts("# oracle batch=");
			rt_putn(b);
			rt_puts(" leaked_errno=");
			rt_putn(g_sh->leaked_errno);
			rt_puts(" unknown_nr_not_enosys=");
			rt_putn(g_sh->unknown_not_enosys);
			rt_puts(" first_nr=");
			rt_putn(g_sh->first_bad_nr);
			rt_puts(" first_ret=");
			rt_putn(g_sh->first_bad_ret);
			rt_puts("\n");
		}
		if (!health_ok(init_pid, &why))
		{
			rt_puts("IR0_FUZZ_HEALTH_LOST reason=");
			rt_puts(why);
			rt_puts(" batch=");
			rt_putn(b);
			rt_puts("\n");
			for (;;)
				rt_sleep_ms(1000);
		}
		done++;
	}

	rt_puts("# IR0_FUZZ_SUMMARY batches=");
	rt_putn(done);
	rt_puts(" calls=");
	rt_putn(calls);
	rt_puts(" child_crashes=");
	rt_putn(crashes);
	rt_puts(" child_hangs=");
	rt_putn(hangs);
	rt_puts(" leaked_errno=");
	rt_putn(leaked);
	rt_puts(" unknown_nr_not_enosys=");
	rt_putn(unknown);
	rt_puts("\nIR0_FUZZ_DONE\n");
	if (init_pid != 1)
		return leaked || unknown ? 1 : 0;
	for (;;)
		rt_sleep_ms(1000);
}
