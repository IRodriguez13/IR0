/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_uaccess.c
 * Description: Selftest: syscalls whose user-pointer checks ran ahead of
 *              their copies and so diverged from Linux (error order, state
 *              consumed or kept). One case per fixed divergence; each
 *              passes on Linux and on IR0. Run by
 *              make ir0-selftest-uaccess-abi-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <netinet/in.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/uio.h>

#define PG RT_PAGE
#define BAD ((void *)RT_KERNEL_ADDR_A)

/* An address inside the user window with nothing mapped at it. */
static void *unmapped_page(void)
{
	long a = S6(SYS_mmap, 0, PG, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (a < 0 && a > -4096)
		return 0;
	if (S2(SYS_munmap, a, PG) != 0)
		return 0;
	return (void *)a;
}

/* 127.0.0.1:@port, written byte by byte (no libc htons on arm64). */
static void inet_lo(struct sockaddr_in *a, unsigned int port)
{
	unsigned char *ip = (unsigned char *)&a->sin_addr;
	unsigned char *pp = (unsigned char *)&a->sin_port;

	rt_fill(a, 0, sizeof(*a));
	a->sin_family = AF_INET;
	pp[0] = (unsigned char)(port >> 8);
	pp[1] = (unsigned char)port;
	ip[0] = 127;
	ip[3] = 1;
}

/*
 * fs/pipe.c do_pipe2(): bad flags are EINVAL before the fds are copied out;
 * a failed copy releases both new descriptors.
 */
int uaccess_pipe2_flags_before_copy(void)
{
	int p[2];
	int q[2];
	void *hole = unmapped_page();

	CHECK(hole != 0);
	EXPECT_ERR(S2(SYS_pipe2, 0, O_APPEND), EINVAL);
	EXPECT_ERR(S2(SYS_pipe2, BAD, O_APPEND), EINVAL);
	EXPECT(S2(SYS_pipe2, p, 0), 0);
	EXPECT(S1(SYS_close, p[0]), 0);
	EXPECT(S1(SYS_close, p[1]), 0);
	EXPECT_ERR(S2(SYS_pipe2, hole, 0), EFAULT);
	EXPECT(S2(SYS_pipe2, q, 0), 0);
	CHECK(q[0] == p[0] && q[1] == p[1]);
	EXPECT(S1(SYS_close, q[0]), 0);
	EXPECT(S1(SYS_close, q[1]), 0);
	return CONF_PASS;
}

/* kernel/exit.c kernel_wait4(): the child is reaped, then the status copy fails. */
int uaccess_wait4_bad_status_reaps(void)
{
	long pid = rt_fork();

	if (pid == 0)
		rt_exit(7);
	CHECK(pid > 0);
	EXPECT_ERR(S4(SYS_wait4, pid, BAD, 0, 0), EFAULT);
	EXPECT_ERR(S4(SYS_wait4, pid, 0, WNOHANG, 0), ECHILD);
	return CONF_PASS;
}

/* kernel/time/posix-timers.c: an unknown clock is EINVAL, whatever @tp is. */
int uaccess_clock_gettime_clock_first(void)
{
	EXPECT_ERR(S2(SYS_clock_gettime, 12345, 0), EINVAL);
	EXPECT_ERR(S2(SYS_clock_gettime, 12345, BAD), EINVAL);
	return CONF_PASS;
}

/* kernel/futex/syscalls.c do_futex(): an unknown op is ENOSYS. */
int uaccess_futex_unknown_op_enosys(void)
{
	EXPECT_ERR(S6(SYS_futex, BAD, 99, 0, 0, 0, 0), ENOSYS);
	return CONF_PASS;
}

/*
 * unix_stream_read_generic(): a faulting copy consumes nothing; the data
 * and the SCM_RIGHTS that ride with it are still there for the next read.
 */
int uaccess_recvmsg_fault_keeps_data_and_rights(void)
{
	char ctl[CMSG_SPACE(sizeof(int))];
	char buf[4];
	struct msghdr m;
	struct iovec v;
	struct cmsghdr *c;
	void *hole = unmapped_page();
	int sv[2];
	int p[2];

	CHECK(hole != 0);
	EXPECT(S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv), 0);
	EXPECT(S2(SYS_pipe2, p, 0), 0);
	rt_fill(&m, 0, sizeof(m));
	rt_fill(ctl, 0, sizeof(ctl));
	v.iov_base = "x";
	v.iov_len = 1;
	m.msg_iov = &v;
	m.msg_iovlen = 1;
	m.msg_control = ctl;
	m.msg_controllen = sizeof(ctl);
	c = CMSG_FIRSTHDR(&m);
	c->cmsg_level = SOL_SOCKET;
	c->cmsg_type = SCM_RIGHTS;
	c->cmsg_len = CMSG_LEN(sizeof(int));
	rt_copy(CMSG_DATA(c), &p[0], sizeof(int));
	EXPECT(S3(SYS_sendmsg, sv[0], &m, 0), 1);

	rt_fill(ctl, 0, sizeof(ctl));
	v.iov_base = hole;
	v.iov_len = sizeof(buf);
	m.msg_controllen = sizeof(ctl);
	EXPECT_ERR(S3(SYS_recvmsg, sv[1], &m, MSG_DONTWAIT), EFAULT);

	rt_fill(ctl, 0, sizeof(ctl));
	v.iov_base = buf;
	m.msg_controllen = sizeof(ctl);
	EXPECT(S3(SYS_recvmsg, sv[1], &m, MSG_DONTWAIT), 1);
	CHECK(buf[0] == 'x');
	c = CMSG_FIRSTHDR(&m);
	CHECK(c != 0 && c->cmsg_level == SOL_SOCKET &&
	      c->cmsg_type == SCM_RIGHTS);
	return CONF_PASS;
}

/* unix_stream_sendmsg(): a faulting iovec sends nothing (EFAULT). */
int uaccess_sendmsg_fault_sends_nothing(void)
{
	char buf[8];
	struct iovec v[2];
	struct msghdr m;
	void *hole = unmapped_page();
	int sv[2];

	CHECK(hole != 0);
	EXPECT(S4(SYS_socketpair, AF_UNIX, SOCK_STREAM, 0, sv), 0);
	v[0].iov_base = "ab";
	v[0].iov_len = 2;
	v[1].iov_base = hole;
	v[1].iov_len = 2;
	rt_fill(&m, 0, sizeof(m));
	m.msg_iov = v;
	m.msg_iovlen = 2;
	EXPECT_ERR(S3(SYS_sendmsg, sv[0], &m, 0), EFAULT);
	EXPECT_ERR(S6(SYS_recvfrom, sv[1], buf, sizeof(buf), MSG_DONTWAIT, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}

/*
 * net/socket.c __sys_recvfrom(): the datagram is received, then
 * move_addr_to_user() fails; the datagram is gone.
 */
int uaccess_recvfrom_bad_addr_consumes(void)
{
	struct sockaddr_in a;
	unsigned int alen = sizeof(a);
	char buf[8];
	long r = S3(SYS_socket, AF_INET, SOCK_DGRAM, 0);

	CHECK(r >= 0);
	inet_lo(&a, 40131);
	EXPECT(S3(SYS_bind, r, &a, sizeof(a)), 0);
	EXPECT(S6(SYS_sendto, r, "x", 1, 0, &a, sizeof(a)), 1);
	EXPECT_ERR(S6(SYS_recvfrom, r, buf, sizeof(buf), MSG_DONTWAIT, BAD,
		      &alen), EFAULT);
	EXPECT_ERR(S6(SYS_recvfrom, r, buf, sizeof(buf), MSG_DONTWAIT, 0, 0),
		   EAGAIN);
	return CONF_PASS;
}

/* mm/mmap.c __get_unmapped_area(): MAP_FIXED past the user space is ENOMEM. */
int uaccess_mmap_fixed_beyond_user_enomem(void)
{
	EXPECT_ERR(S6(SYS_mmap, BAD, PG, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0), ENOMEM);
	return CONF_PASS;
}

/* mm/mprotect.c do_mprotect_pkey(): no mapping there is ENOMEM. */
int uaccess_mprotect_beyond_user_enomem(void)
{
	EXPECT_ERR(S3(SYS_mprotect, BAD, PG, PROT_READ), ENOMEM);
	return CONF_PASS;
}
