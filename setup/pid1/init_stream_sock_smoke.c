/**
 * IR0 — AF_UNIX + TCP loopback stream smoke
 */
/* SPDX-License-Identifier: GPL-3.0-only */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

static int test_unix(void)
{
	int srv, cli, acc;
	struct sockaddr_un addr;
	char buf[32];
	ssize_t n;
	const char *msg = "UNIXOK";

	unlink("/tmp/ir0.sock");
	srv = socket(AF_UNIX, SOCK_STREAM, 0);
	if (srv < 0)
		return 2;
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, "/tmp/ir0.sock", sizeof(addr.sun_path) - 1);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		return 3;
	if (listen(srv, 1) != 0)
		return 4;
	cli = socket(AF_UNIX, SOCK_STREAM, 0);
	if (cli < 0)
		return 5;
	if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		return 6;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		return 7;
	if (send(cli, msg, 6, 0) != 6)
		return 8;
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 6 || memcmp(buf, msg, 6) != 0)
		return 9;
	close(acc);
	close(cli);
	close(srv);
	return 0;
}

static int test_tcp(void)
{
	int srv, cli, acc;
	struct sockaddr_in addr;
	char buf[32];
	ssize_t n;
	const char *msg = "TCPOK";

	srv = socket(AF_INET, SOCK_STREAM, 0);
	if (srv < 0)
		return 12;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(7777);
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	if (bind(srv, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		return 13;
	if (listen(srv, 1) != 0)
		return 14;
	cli = socket(AF_INET, SOCK_STREAM, 0);
	if (cli < 0)
		return 15;
	if (connect(cli, (struct sockaddr *)&addr, sizeof(addr)) != 0)
		return 16;
	acc = accept(srv, NULL, NULL);
	if (acc < 0)
		return 17;
	if (send(cli, msg, 5, 0) != 5)
		return 18;
	n = recv(acc, buf, sizeof(buf), 0);
	if (n != 5 || memcmp(buf, msg, 5) != 0)
		return 19;
	close(acc);
	close(cli);
	close(srv);
	return 0;
}

int main(void)
{
	int r;

	r = test_unix();
	if (r != 0)
		return r;
	r = test_tcp();
	if (r != 0)
		return r;
	(void)write(1, "STREAM_SOCK_OK\n", 15);
	(void)write(1, "STREAM_SENDRECV_OK\n", 19);
	return 0;
}
