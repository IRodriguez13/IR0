/**
 * IR0 — F8 wire TCP listen+accept smoke (host → guest via QEMU hostfwd)
 */
/* SPDX-License-Identifier: GPL-3.0-only */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define LISTEN_PORT 7777
#define EXPECT "LISTENOK"
#define ACCEPT_TRIES 400
#define RECV_TRIES 200
#define EOF_TRIES 200

static int tag_ok(const char *t)
{
	printf("%s\n", t);
	fflush(stdout);
	return 0;
}

static int fail(const char *t, int err)
{
	printf("%s errno=%d\n", t, err);
	fflush(stdout);
	return 1;
}

static void nap(void)
{
	usleep(25000);
}

int main(void)
{
	int srv;
	int acc = -1;
	struct sockaddr_in addr;
	char buf[64];
	ssize_t n;
	int i;
	int saw_eof = 0;

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return fail("F8_TCP_LISTEN_SOCK_FAIL", errno);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(LISTEN_PORT);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
	{
		close(srv);
		return fail("F8_TCP_LISTEN_BIND_FAIL", errno);
	}
	if (listen(srv, 1) != 0)
	{
		close(srv);
		return fail("F8_TCP_LISTEN_LISTEN_FAIL", errno);
	}
	tag_ok("F8_TCP_LISTEN_BOUND_OK");

	for (i = 0; i < ACCEPT_TRIES; i++)
	{
		acc = accept(srv, NULL, NULL);
		if (acc >= 0)
			break;
		if (errno != EAGAIN && errno != EWOULDBLOCK)
		{
			close(srv);
			return fail("F8_TCP_LISTEN_ACCEPT_FAIL", errno);
		}
		nap();
	}
	if (acc < 0)
	{
		close(srv);
		return fail("F8_TCP_LISTEN_ACCEPT_TIMEOUT", ETIMEDOUT);
	}
	tag_ok("F8_TCP_LISTEN_ACCEPT_OK");

	memset(buf, 0, sizeof(buf));
	n = 0;
	for (i = 0; i < RECV_TRIES; i++)
	{
		n = recv(acc, buf, sizeof(buf) - 1, 0);
		if (n > 0)
			break;
		if (n == 0)
			return fail("F8_TCP_LISTEN_EARLY_EOF", 0);
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			close(acc);
			close(srv);
			return fail("F8_TCP_LISTEN_RECV_FAIL", errno);
		}
		nap();
	}
	if (n <= 0)
	{
		close(acc);
		close(srv);
		return fail("F8_TCP_LISTEN_RECV_TIMEOUT", ETIMEDOUT);
	}
	if (n < (ssize_t)strlen(EXPECT) || memcmp(buf, EXPECT, strlen(EXPECT)) != 0)
	{
		close(acc);
		close(srv);
		return fail("F8_TCP_LISTEN_PAYLOAD_FAIL", 0);
	}

	for (i = 0; i < EOF_TRIES; i++)
	{
		n = recv(acc, buf, sizeof(buf) - 1, 0);
		if (n == 0)
		{
			saw_eof = 1;
			break;
		}
		if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK)
		{
			close(acc);
			close(srv);
			return fail("F8_TCP_LISTEN_EOF_FAIL", errno);
		}
		nap();
	}
	close(acc);
	close(srv);

	if (!saw_eof)
		return fail("F8_TCP_LISTEN_EOF_TIMEOUT", ETIMEDOUT);

	tag_ok("F8_TCP_LISTEN_EOF_OK");
	tag_ok("F8_TCP_LISTEN_OK");
	return 0;
}
