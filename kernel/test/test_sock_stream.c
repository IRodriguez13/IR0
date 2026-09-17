/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_sock_stream.c
 * Description: In-kernel AF_UNIX stream queue contract tests.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "test/ktest_harness.h"
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/socket.h>
#include <ir0/sock_stream.h>

#define KTEST_STREAM_BYTES (64U * 1024U + 37U)
#define KTEST_PEEK_BYTES   5003U

void ktest_sock_stream_accept_fifo_backlog(void)
{
	struct sock_stream *listener = NULL;
	struct sock_stream *client_a = NULL;
	struct sock_stream *client_b = NULL;
	struct sock_stream *client_c = NULL;
	struct sock_stream *accepted_a = NULL;
	struct sock_stream *accepted_b = NULL;
	struct sock_stream *accepted_c = NULL;
	char byte = 0;

	KTEST_BEGIN("sock_stream_accept_fifo_backlog");

	listener = sock_stream_create(IR0_AF_UNIX);
	client_a = sock_stream_create(IR0_AF_UNIX);
	client_b = sock_stream_create(IR0_AF_UNIX);
	client_c = sock_stream_create(IR0_AF_UNIX);
	KASSERT(listener && client_a && client_b && client_c);
	if (!listener || !client_a || !client_b || !client_c)
		goto out;

	KASSERT_EQ(sock_stream_bind_unix(listener, "/ktest.sock"), 0);
	KASSERT_EQ(sock_stream_listen(listener, 2), 0);
	KASSERT_EQ(sock_stream_connect_unix(client_a, "/ktest.sock"), 0);
	KASSERT_EQ(sock_stream_connect_unix(client_b, "/ktest.sock"), 0);
	KASSERT_EQ(sock_stream_connect_unix(client_c, "/ktest.sock"),
		   -ECONNREFUSED);
	KASSERT(sock_stream_poll_readable(listener));

	KASSERT_EQ(sock_stream_send(client_a, "A", 1), 1);
	KASSERT_EQ(sock_stream_send(client_b, "B", 1), 1);
	accepted_a = sock_stream_accept(listener);
	accepted_b = sock_stream_accept(listener);
	KASSERT(accepted_a && accepted_b);
	if (accepted_a)
	{
		KASSERT_EQ(sock_stream_recv(accepted_a, &byte, 1), 1);
		KASSERT_EQ(byte, 'A');
	}
	if (accepted_b)
	{
		KASSERT_EQ(sock_stream_recv(accepted_b, &byte, 1), 1);
		KASSERT_EQ(byte, 'B');
	}
	KASSERT(sock_stream_accept(listener) == NULL);

	/* A failed full-backlog connect must leave the client reusable. */
	KASSERT_EQ(sock_stream_connect_unix(client_c, "/ktest.sock"), 0);
	accepted_c = sock_stream_accept(listener);
	KASSERT(accepted_c != NULL);

out:
	if (accepted_a)
		sock_stream_release(accepted_a);
	if (accepted_b)
		sock_stream_release(accepted_b);
	if (accepted_c)
		sock_stream_release(accepted_c);
	if (client_a)
		sock_stream_release(client_a);
	if (client_b)
		sock_stream_release(client_b);
	if (client_c)
		sock_stream_release(client_c);
	if (listener)
		sock_stream_release(listener);

	KTEST_END();
}

void ktest_sock_stream_segmented_queue(void)
{
	struct sock_stream *sender = NULL;
	struct sock_stream *receiver = NULL;
	unsigned char *source = NULL;
	unsigned char *result = NULL;
	unsigned char *peek = NULL;
	size_t offset;
	size_t filled = 0;
	ssize_t n;

	KTEST_BEGIN("sock_stream_segmented_queue");

	KASSERT_EQ(sock_stream_socketpair(&sender, &receiver), 0);
	if (!sender || !receiver)
		goto out;
	source = kmalloc_try(KTEST_STREAM_BYTES);
	result = kmalloc_try(KTEST_STREAM_BYTES);
	peek = kmalloc_try(KTEST_PEEK_BYTES);
	KASSERT(source && result && peek);
	if (!source || !result || !peek)
		goto out;

	for (offset = 0; offset < KTEST_STREAM_BYTES; offset++)
		source[offset] = (unsigned char)((offset * 29U + offset / 251U) & 0xffU);

	n = sock_stream_send(sender, source, KTEST_STREAM_BYTES);
	KASSERT_EQ(n, (ssize_t)KTEST_STREAM_BYTES);
	KASSERT_EQ(sock_stream_buf_count(receiver), (int)KTEST_STREAM_BYTES);
	KASSERT(sock_stream_poll_readable(receiver));

	n = sock_stream_recv_flags(receiver, peek, KTEST_PEEK_BYTES, MSG_PEEK);
	KASSERT_EQ(n, (ssize_t)KTEST_PEEK_BYTES);
	KASSERT_EQ(sock_stream_buf_count(receiver), (int)KTEST_STREAM_BYTES);
	for (offset = 0; offset < KTEST_PEEK_BYTES; offset++)
		KASSERT_EQ(peek[offset], source[offset]);

	/* Irregular reads exercise partial head chunks and every segment edge. */
	offset = 0;
	while (offset < KTEST_STREAM_BYTES) {
		size_t request = 3001U;
		if (request > KTEST_STREAM_BYTES - offset)
			request = KTEST_STREAM_BYTES - offset;
		n = sock_stream_recv(receiver, result + offset, request);
		KASSERT_EQ(n, (ssize_t)request);
		if (n <= 0)
			break;
		offset += (size_t)n;
	}
	KASSERT_EQ(offset, (size_t)KTEST_STREAM_BYTES);
	for (offset = 0; offset < KTEST_STREAM_BYTES; offset++)
		KASSERT_EQ(result[offset], source[offset]);
	KASSERT_EQ(sock_stream_buf_count(receiver), 0);
	KASSERT_EQ(sock_stream_recv(receiver, result, 1), -EAGAIN);
	KASSERT(sock_stream_poll_writable(sender));

	/* Fill the advertised receive capacity and verify sender backpressure. */
	while (sock_stream_buf_space(receiver) > 0) {
		size_t request = KTEST_STREAM_BYTES;
		int space = sock_stream_buf_space(receiver);
		if (request > (size_t)space)
			request = (size_t)space;
		n = sock_stream_send(sender, source, request);
		KASSERT_EQ(n, (ssize_t)request);
		if (n <= 0)
			break;
		filled += (size_t)n;
	}
	KASSERT_EQ(sock_stream_buf_space(receiver), 0);
	KASSERT(!sock_stream_poll_writable(sender));
	KASSERT_EQ(sock_stream_send(sender, source, 1), 0);
	offset = 0;
	while (offset < filled) {
		size_t request = 3001U;
		size_t j;
		int mismatch = 0;

		if (request > filled - offset)
			request = filled - offset;
		n = sock_stream_recv(receiver, result, request);
		KASSERT_EQ(n, (ssize_t)request);
		if (n <= 0)
			break;
		for (j = 0; j < (size_t)n; j++) {
			if (result[j] != source[(offset + j) % KTEST_STREAM_BYTES]) {
				mismatch = 1;
				break;
			}
		}
		KASSERT(!mismatch);
		if (mismatch)
			break;
		offset += (size_t)n;
	}
	KASSERT_EQ(offset, filled);
	KASSERT_EQ(sock_stream_buf_count(receiver), 0);
	KASSERT(sock_stream_poll_writable(sender));

out:
	if (peek)
		kfree(peek);
	if (result)
		kfree(result);
	if (source)
		kfree(source);
	if (sender)
		sock_stream_release(sender);
	if (receiver)
		sock_stream_release(receiver);

	KTEST_END();
}
