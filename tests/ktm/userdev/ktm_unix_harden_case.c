/**
 * IR0 userspace — KTM unix harden case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_unix_harden_case.c
 * Description: getpeername from peer + multi-fd SCM_RIGHTS in one recvmsg.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <sys/un.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_getpeername(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	struct sockaddr_un peer;
	socklen_t plen;
	const char name[] = "ir0peer";

	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0)
		return -1;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	addr.sun_path[0] = '\0';
	memcpy(addr.sun_path + 1, name, sizeof(name) - 1);
	if (bind(srv, (struct sockaddr *)&addr,
		 offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail_srv;
	if (listen(srv, 1) != 0)
		goto fail_srv;
	cli = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cli < 0)
		goto fail_srv;
	if (connect(cli, (struct sockaddr *)&addr,
		    offsetof(struct sockaddr_un, sun_path) + 1 + sizeof(name) - 1) != 0)
		goto fail_cli;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		goto fail_cli;
	plen = sizeof(peer);
	memset(&peer, 0, sizeof(peer));
	if (getpeername(cli, (struct sockaddr *)&peer, &plen) != 0)
		goto fail_acc;
	if (peer.sun_family != AF_UNIX || peer.sun_path[0] != '\0')
		goto fail_acc;
	if (memcmp(peer.sun_path + 1, name, sizeof(name) - 1) != 0)
		goto fail_acc;
	close(acc);
	close(cli);
	close(srv);
	return 0;

fail_acc:
	close(acc);
fail_cli:
	close(cli);
fail_srv:
	close(srv);
	return -1;
}

static int test_scm_multi(void)
{
	int sv[2];
	int p0[2];
	int p1[2];
	char buf[8];
	struct msghdr msg;
	struct iovec iov;
	char cmsgbuf[CMSG_SPACE(sizeof(int) * 2)];
	struct cmsghdr *cmsg;
	int *fds;
	int recv0 = -1;
	int recv1 = -1;
	ssize_t n;
	char byte = 'M';

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return -1;
	if (pipe(p0) != 0 || pipe(p1) != 0)
		goto fail_sv;
	if (write(p0[1], "A", 1) != 1 || write(p1[1], "B", 1) != 1)
		goto fail_pipes;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = &byte;
	iov.iov_len = 1;
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	cmsg = CMSG_FIRSTHDR(&msg);
	cmsg->cmsg_level = SOL_SOCKET;
	cmsg->cmsg_type = SCM_RIGHTS;
	cmsg->cmsg_len = CMSG_LEN(sizeof(int) * 2);
	fds = (int *)CMSG_DATA(cmsg);
	fds[0] = p0[0];
	fds[1] = p1[0];
	msg.msg_controllen = cmsg->cmsg_len;
	if (sendmsg(sv[0], &msg, 0) < 0)
		goto fail_pipes;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	n = recvmsg(sv[1], &msg, 0);
	if (n < 0)
		goto fail_pipes;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
	{
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
		{
			size_t nbytes = cmsg->cmsg_len - CMSG_LEN(0);
			fds = (int *)CMSG_DATA(cmsg);
			if (nbytes < sizeof(int) * 2)
				goto fail_pipes;
			recv0 = fds[0];
			recv1 = fds[1];
		}
	}
	if (recv0 < 0 || recv1 < 0)
		goto fail_pipes;
	memset(buf, 0, sizeof(buf));
	if (read(recv0, buf, 1) != 1 || buf[0] != 'A')
		goto fail_recv;
	if (read(recv1, buf, 1) != 1 || buf[0] != 'B')
		goto fail_recv;
	close(recv0);
	close(recv1);
	close(p0[0]);
	close(p0[1]);
	close(p1[0]);
	close(p1[1]);
	close(sv[0]);
	close(sv[1]);
	return 0;

fail_recv:
	if (recv0 >= 0)
		close(recv0);
	if (recv1 >= 0)
		close(recv1);
fail_pipes:
	close(p0[0]);
	close(p0[1]);
	close(p1[0]);
	close(p1[1]);
fail_sv:
	close(sv[0]);
	close(sv[1]);
	return -1;
}

int main(void)
{
	int kfd;
	int fails = 0;
	ktm_user_caps_t caps;

	kfd = ktm_open();
	if (kfd < 0)
	{
		say("KTM_UNIX_HARDEN_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_UNIX_HARDEN_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "unix_harden") != 0)
	{
		say("KTM_UNIX_HARDEN_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_getpeername() != 0)
	{
		(void)ktm_assert_true(kfd, "getpeername", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "getpeername", 1);
		say("GETPEERNAME_OK\n");
	}
	if (test_scm_multi() != 0)
	{
		(void)ktm_assert_true(kfd, "scm_multi", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "scm_multi", 1);
		say("SCM_MULTI_OK\n");
	}
	if (fails == 0)
		say("KTM_UNIX_HARDEN_OK\n");
	(void)ktm_case_end(kfd, "unix_harden", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
