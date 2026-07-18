/**
 * IR0 userspace — KTM SCM_RIGHTS case
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: ktm_scm_rights_case.c
 * Description: Pass a pipe fd over AF_UNIX socketpair via SCM_RIGHTS.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <string.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>

#include "libktm_user.h"

static void say(const char *s)
{
	(void)write(1, s, strlen(s));
}

static int test_scm_rights(void)
{
	int sv[2];
	int pfd[2];
	char buf[8];
	struct msghdr msg;
	struct iovec iov;
	char cmsgbuf[CMSG_SPACE(sizeof(int))];
	struct cmsghdr *cmsg;
	int recvfd = -1;
	ssize_t n;
	char byte = 'A';

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
		return -1;
	if (pipe(pfd) != 0)
		goto fail_sv;
	if (write(pfd[1], "XY", 2) != 2)
		goto fail_pipe;

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
	cmsg->cmsg_len = CMSG_LEN(sizeof(int));
	memcpy(CMSG_DATA(cmsg), &pfd[0], sizeof(int));
	msg.msg_controllen = cmsg->cmsg_len;
	if (sendmsg(sv[0], &msg, 0) < 0)
		goto fail_pipe;

	memset(&msg, 0, sizeof(msg));
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cmsgbuf;
	msg.msg_controllen = sizeof(cmsgbuf);
	n = recvmsg(sv[1], &msg, 0);
	if (n < 0)
		goto fail_pipe;
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg))
	{
		if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS)
			memcpy(&recvfd, CMSG_DATA(cmsg), sizeof(int));
	}
	if (recvfd < 0)
		goto fail_pipe;
	memset(buf, 0, sizeof(buf));
	n = read(recvfd, buf, 2);
	if (n != 2 || buf[0] != 'X' || buf[1] != 'Y')
		goto fail_recv;
	close(recvfd);
	close(pfd[0]);
	close(pfd[1]);
	close(sv[0]);
	close(sv[1]);
	return 0;

fail_recv:
	close(recvfd);
fail_pipe:
	close(pfd[0]);
	close(pfd[1]);
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
		say("KTM_SCM_RIGHTS_FAIL open\n");
		return 1;
	}
	if (ktm_get_caps(kfd, &caps) != 0 || !(caps.caps & KTM_CAP_USERDEV))
	{
		say("KTM_SCM_RIGHTS_FAIL caps\n");
		ktm_close(kfd);
		return 1;
	}
	(void)ktm_reset(kfd);
	if (ktm_case_begin(kfd, "scm_rights") != 0)
	{
		say("KTM_SCM_RIGHTS_FAIL case_begin\n");
		ktm_close(kfd);
		return 1;
	}
	if (test_scm_rights() != 0)
	{
		(void)ktm_assert_true(kfd, "scm_rights", 0);
		fails++;
	}
	else
	{
		(void)ktm_assert_true(kfd, "scm_rights", 1);
		say("SCM_RIGHTS_OK\n");
		say("KTM_SCM_RIGHTS_OK\n");
	}
	(void)ktm_case_end(kfd, "scm_rights", fails == 0 ? 0 : 1);
	ktm_close(kfd);
	say(fails == 0 ? "KTM_USERDEV_OK\n" : "KTM_USERDEV_FAIL\n");
	return fails == 0 ? 0 : 1;
}
