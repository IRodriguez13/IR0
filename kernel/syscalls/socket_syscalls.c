/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: socket_syscalls.c
 * Description: minimal AF_INET SOCK_DGRAM socket syscalls
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "socket_syscalls.h"
#include "syscalls_glue.h"
#include <errno.h>
#include <ir0/syscalls_kernel.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/net.h>
#include <ir0/open_flags.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/socket.h>
#include <config.h>
#include <string.h>

#define IPPROTO_UDP 17
#define MSG_DONTWAIT 0x40

static int sock_alloc_fd_any(void *sock, int is_stream)
{
	fd_entry_t *fd_table;
	int fd;

	if (!sock || !current_process)
		return -EINVAL;

	fd_table = get_process_fd_table();
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!fd_table[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
	{
		if (is_stream)
			sock_stream_release(sock);
		else
			sock_udp_release(sock);
		return -EMFILE;
	}

	fd_table[fd].in_use = true;
	fd_table[fd].path[0] = '\0';
	fd_table[fd].offset = 0;
	fd_table[fd].flags = 0;
	fd_table[fd].fd_flags = 0;
	fd_table[fd].vfs_file = sock;
	fd_table[fd].is_pipe = false;
	fd_table[fd].pipe_end = -1;
	fd_table[fd].is_devfs = false;
	fd_table[fd].dev_device_id = 0;
	fd_table[fd].is_socket = true;
	fase48_note_fd_created();
	return fd;
}

static int sock_alloc_fd(struct sock_udp *sock)
{
	return sock_alloc_fd_any(sock, 0);
}

static struct sock_udp *sock_fd_lookup(int fd)
{
	fd_entry_t *fd_table;
	void *p;

	if (!current_process)
		return NULL;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use || !fd_table[fd].is_socket)
		return NULL;
	p = fd_table[fd].vfs_file;
	if (sock_stream_is(p))
		return NULL;
	return (struct sock_udp *)p;
}

static struct sock_stream *sock_stream_fd_lookup(int fd)
{
	fd_entry_t *fd_table;
	void *p;

	if (!current_process)
		return NULL;
	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return NULL;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use || !fd_table[fd].is_socket)
		return NULL;
	p = fd_table[fd].vfs_file;
	if (!sock_stream_is(p))
		return NULL;
	return (struct sock_stream *)p;
}

static int sock_nonblock_fd(int fd, int flags)
{
	fd_entry_t *fd_table;

	if (flags & MSG_DONTWAIT)
		return 1;
	fd_table = get_process_fd_table();
	if (fd_table[fd].flags & O_NONBLOCK)
		return 1;
	return 0;
}

int64_t sys_socket(int domain, int type, int protocol)
{
	int fd;

#if !CONFIG_ENABLE_NETWORKING
	(void)domain;
	(void)type;
	(void)protocol;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;

	if (type == SOCK_STREAM && (domain == AF_UNIX || domain == AF_INET))
	{
		struct sock_stream *ss;

		if (protocol != 0 && protocol != 6) /* IPPROTO_TCP */
			return -EPROTONOSUPPORT;
		ss = sock_stream_create(domain == AF_UNIX ? IR0_AF_UNIX : IR0_AF_INET);
		if (!ss)
			return -ENOMEM;
		fd = sock_alloc_fd_any(ss, 1);
		return fd < 0 ? fd : fd;
	}

	if (domain != AF_INET)
		return -EAFNOSUPPORT;
	if (type != SOCK_DGRAM)
		return -EPROTOTYPE;
	if (protocol != 0 && protocol != IPPROTO_UDP)
		return -EPROTONOSUPPORT;

	{
		struct sock_udp *sock = sock_udp_create();

		if (!sock)
			return -ENOMEM;
		fd = sock_alloc_fd(sock);
		return fd;
	}
}

int64_t sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!addr || addrlen < 2)
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		uint16_t family = 0;

		if (copy_from_user(&family, addr, sizeof(family)) != 0)
			return -EFAULT;
		if (family == AF_UNIX)
		{
			struct sockaddr_un sun;

			if (addrlen < sizeof(sun.sun_family) + 1)
				return -EINVAL;
			memset(&sun, 0, sizeof(sun));
			if (copy_from_user(&sun, addr,
					   addrlen < sizeof(sun) ? addrlen : sizeof(sun)) != 0)
				return -EFAULT;
			return sock_stream_bind_unix(ss, sun.sun_path);
		}
		if (family == AF_INET)
		{
			struct sockaddr_in sin;

			if (addrlen < sizeof(sin))
				return -EINVAL;
			if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
				return -EFAULT;
			return sock_stream_bind_inet(ss, ntohs(sin.sin_port));
		}
		return -EAFNOSUPPORT;
	}

	{
		struct sockaddr_in sin;

		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
			return -EFAULT;
		if (sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
		sock = sock_fd_lookup(fd);
		if (!sock)
			return -ENOTSOCK;
		ret = sock_udp_bind(sock, ntohs(sin.sin_port));
		return ret;
	}
}

ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sockaddr_in sin;
	struct sock_udp *sock;
	uint8_t *kbuf;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)buf;
	(void)len;
	(void)flags;
	(void)dest_addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!buf)
		return -EINVAL;
	if (validate_userspace_buffer(buf, len) != 0)
		return -EFAULT;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		ssize_t n;

		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buf, len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		n = sock_stream_send(ss, kbuf, len);
		kfree(kbuf);
		(void)flags;
		(void)dest_addr;
		(void)addrlen;
		return n;
	}

	if (!dest_addr || addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;
	if (copy_from_user(&sin, dest_addr, sizeof(sin)) != 0)
		return -EFAULT;
	if (sin.sin_family != AF_INET)
		return -EAFNOSUPPORT;

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;

	kbuf = kmalloc(len);
	if (!kbuf)
		return -ENOMEM;
	if (copy_from_user(kbuf, buf, len) != 0)
	{
		kfree(kbuf);
		return -EFAULT;
	}

	ret = sock_udp_sendto(sock, sin.sin_addr, ntohs(sin.sin_port), kbuf, len);
	kfree(kbuf);
	if (ret < 0)
		return ret;
	(void)flags;
	return (ssize_t)ret;
}

ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	uint16_t src_port = 0;
	uint8_t *kbuf;
	ssize_t n;
	struct sockaddr_in sin;
	socklen_t out_len = sizeof(sin);
	int nb;
	uint32_t src_ip_be = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)buf;
	(void)len;
	(void)flags;
	(void)src_addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!buf)
		return -EINVAL;
	if (validate_userspace_buffer(buf, len) != 0)
		return -EFAULT;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		n = sock_stream_recv(ss, kbuf, len);
		if (n < 0)
		{
			kfree(kbuf);
			return n;
		}
		if (n > 0 && copy_to_user(buf, kbuf, (size_t)n) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		kfree(kbuf);
		(void)flags;
		(void)src_addr;
		(void)addrlen;
		return n;
	}

	if (addrlen)
	{
		if (validate_userspace_buffer(addrlen, sizeof(socklen_t)) != 0)
			return -EFAULT;
		if (copy_from_user(&out_len, addrlen, sizeof(socklen_t)) != 0)
			return -EFAULT;
	}
	if (src_addr && out_len > 0)
	{
		if (validate_userspace_buffer(src_addr, out_len) != 0)
			return -EFAULT;
	}

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;

	nb = sock_nonblock_fd(fd, flags);
	kbuf = kmalloc(len);
	if (!kbuf)
		return -ENOMEM;

	n = sock_udp_recvfrom(sock, kbuf, len, nb ? MSG_DONTWAIT : 0,
			      &src_ip_be, &src_port);
	if (n < 0)
	{
		kfree(kbuf);
		return n;
	}
	if (copy_to_user(buf, kbuf, (size_t)n) != 0)
	{
		kfree(kbuf);
		return -EFAULT;
	}
	kfree(kbuf);

	if (src_addr && out_len >= sizeof(sin))
	{
		memset(&sin, 0, sizeof(sin));
		sin.sin_family = AF_INET;
		sin.sin_port = htons(src_port);
		sin.sin_addr = src_ip_be;
		if (copy_to_user(src_addr, &sin, sizeof(sin)) != 0)
			return -EFAULT;
		if (addrlen)
		{
			socklen_t slen = sizeof(sin);

			if (copy_to_user(addrlen, &slen, sizeof(slen)) != 0)
				return -EFAULT;
		}
	}
	return n;
}

int64_t sys_connect(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	struct sock_stream *ss;
	struct sock_udp *sock;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!addr || addrlen < 2)
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		uint16_t family = 0;

		if (copy_from_user(&family, addr, sizeof(family)) != 0)
			return -EFAULT;
		if (family == AF_UNIX)
		{
			struct sockaddr_un sun;

			memset(&sun, 0, sizeof(sun));
			if (copy_from_user(&sun, addr,
					   addrlen < sizeof(sun) ? addrlen : sizeof(sun)) != 0)
				return -EFAULT;
			return sock_stream_connect_unix(ss, sun.sun_path);
		}
		if (family == AF_INET)
		{
			struct sockaddr_in sin;

			if (addrlen < sizeof(sin))
				return -EINVAL;
			if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
				return -EFAULT;
			return sock_stream_connect_inet(ss, sin.sin_addr, ntohs(sin.sin_port));
		}
		return -EAFNOSUPPORT;
	}

	{
		struct sockaddr_in sin;

		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (copy_from_user(&sin, addr, sizeof(sin)) != 0)
			return -EFAULT;
		if (sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
		sock = sock_fd_lookup(fd);
		if (!sock)
			return -ENOTSOCK;
		ret = sock_udp_connect(sock, sin.sin_addr, ntohs(sin.sin_port));
		return ret;
	}
}

int64_t sys_listen(int fd, int backlog)
{
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)backlog;
	return -ENOSYS;
#endif
	if (!current_process)
		return -ESRCH;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return sock_stream_listen(ss, backlog);
}

int64_t sys_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	struct sock_stream *ss;
	struct sock_stream *child;
	int nfd;

	(void)addr;
	(void)addrlen;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;

	ss = sock_stream_fd_lookup(fd);
	if (!ss)
	{
		if (!sock_fd_lookup(fd))
			return -ENOTSOCK;
		return -EOPNOTSUPP;
	}
	child = sock_stream_accept(ss);
	if (!child)
		return -EAGAIN;
	nfd = sock_alloc_fd_any(child, 1);
	return nfd;
}

int64_t sys_socketpair(int domain, int type, int protocol, int *sv)
{
	struct sock_stream *a;
	struct sock_stream *b;
	int fd0;
	int fd1;
	int sv_k[2];
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)domain;
	(void)type;
	(void)protocol;
	(void)sv;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (!sv)
		return -EFAULT;
	if (domain != AF_UNIX)
		return -EAFNOSUPPORT;
	if (type != SOCK_STREAM)
		return -EPROTOTYPE;
	if (protocol != 0)
		return -EPROTONOSUPPORT;

	ret = sock_stream_socketpair(&a, &b);
	if (ret < 0)
		return ret;

	fd0 = sock_alloc_fd_any(a, 1);
	if (fd0 < 0)
	{
		sock_stream_release(b);
		return fd0;
	}
	fd1 = sock_alloc_fd_any(b, 1);
	if (fd1 < 0)
	{
		fd_entry_t *fd_table = get_process_fd_table();

		if (fd_table && fd0 >= 0 && fd0 < MAX_FDS_PER_PROCESS &&
		    fd_table[fd0].in_use)
		{
			fd_table[fd0].in_use = false;
			fd_table[fd0].vfs_file = NULL;
			fd_table[fd0].is_socket = false;
		}
		sock_stream_release(a);
		return fd1;
	}

	sv_k[0] = fd0;
	sv_k[1] = fd1;
	if (copy_to_user(sv, sv_k, sizeof(sv_k)) != 0)
	{
		fd_entry_t *fd_table = get_process_fd_table();

		if (fd_table)
		{
			if (fd0 >= 0 && fd0 < MAX_FDS_PER_PROCESS &&
			    fd_table[fd0].in_use)
			{
				fd_table[fd0].in_use = false;
				fd_table[fd0].vfs_file = NULL;
				fd_table[fd0].is_socket = false;
			}
			if (fd1 >= 0 && fd1 < MAX_FDS_PER_PROCESS &&
			    fd_table[fd1].in_use)
			{
				fd_table[fd1].in_use = false;
				fd_table[fd1].vfs_file = NULL;
				fd_table[fd1].is_socket = false;
			}
		}
		sock_stream_release(a);
		sock_stream_release(b);
		return -EFAULT;
	}
	return 0;
}
