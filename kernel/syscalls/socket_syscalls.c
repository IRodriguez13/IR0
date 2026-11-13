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
#include "io_syscalls.h"
#include <errno.h>
#include <kernel/syscalls.h>
#include <ir0/process.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/net.h>
#include <ir0/open_flags.h>
#include <ir0/sock_udp.h>
#include <ir0/sock_stream.h>
#include <ir0/sock_icmp.h>
#include <ir0/socket.h>
#include <ir0/time.h>
#include <ir0/signals.h>
#include <ir0/arch_cpu.h>
#include <ir0/clock.h>
#include <ir0/pipe.h>
#include <ir0/pipe_fd.h>
#include <ir0/vfs.h>
#include <ir0/memfd.h>
#include <ir0/named_socket.h>
#include <ir0/path_routed.h>
#include <ir0/path_user.h>
#include <ir0/logging.h>
#include <ir0/devfs.h>
#include <ir0/uio.h>
#include <config.h>
#include <string.h>

/* Linux struct sockaddr_storage: the largest address a syscall copies in. */
union sock_kaddr
{
	struct sockaddr sa;
	struct sockaddr_in sin;
	struct sockaddr_un sun;
	uint8_t raw[128];
};

/* A staged SCM_RIGHTS descriptor travels as one opaque rights entry. */
_Static_assert(sizeof(fd_entry_t) <= SOCK_STREAM_RIGHTS_ENTRY_SIZE,
	       "fd_entry_t outgrew SOCK_STREAM_RIGHTS_ENTRY_SIZE");

/*
 * net/socket.c move_addr_to_kernel(): a length past sockaddr_storage is
 * EINVAL, an unreadable address EFAULT. The tail past @len reads as zero.
 */
static int sock_addr_copy(union sock_kaddr *k, const struct sockaddr *uaddr,
			  socklen_t len)
{
	memset(k, 0, sizeof(*k));
	if (len > sizeof(k->raw))
		return -EINVAL;
	if (len && copy_from_user(k->raw, uaddr, len) != 0)
		return -EFAULT;
	return 0;
}

/*
 * unix_validate_addr() and unix_mkname_bsd(): check a copied sockaddr_un
 * and fill in the key that finds its socket, the path resolved against
 * the caller's cwd and root or the abstract name.
 */
static int unix_addr_get(union sock_kaddr *k, socklen_t len,
			 struct sock_unix_addr *ua)
{
	char resolved[256];
	size_t plen;
	int rc;

	memset(ua, 0, sizeof(*ua));
	if (len < offsetof(struct sockaddr_un, sun_path) ||
	    len > sizeof(struct sockaddr_un) || k->sun.sun_family != AF_UNIX)
		return -EINVAL;
	plen = len - offsetof(struct sockaddr_un, sun_path);
	if (plen == 0 || k->sun.sun_path[0] == '\0')
	{
		if (plen >= sizeof(ua->key))
			return -EINVAL;
		memcpy(ua->key, k->sun.sun_path, plen);
		ua->key_len = plen;
		ua->is_abstract = 1;
		memcpy(ua->sun_path, k->sun.sun_path, plen);
		ua->sun_len = len;
		return 0;
	}
	/* sun_path may fill all 108 bytes; the union has room for the NUL. */
	k->raw[len] = '\0';
	plen = strlen(k->sun.sun_path);
	memcpy(ua->sun_path, k->sun.sun_path, plen);
	ua->sun_len = offsetof(struct sockaddr_un, sun_path) + plen + 1;
	rc = ir0_resolve_kpath_at(IR0_AT_FDCWD, k->sun.sun_path, resolved,
				  sizeof(resolved), current_process->cwd,
				  current_process->root);
	if (rc != 0)
		return rc;
	plen = strlen(resolved);
	if (plen >= sizeof(ua->key))
		return -ENAMETOOLONG;
	memcpy(ua->key, resolved, plen + 1);
	ua->key_len = plen;
	return 0;
}

/*
 * unix_getname() + move_addr_to_user(): copy at most *@addrlen bytes of
 * the address @ua (family only when NULL: unbound) and store its full
 * length in *@addrlen.
 */
static int64_t unix_fill_sockaddr(const struct sock_unix_addr *ua,
				  struct sockaddr *addr, socklen_t *addrlen)
{
	union sock_kaddr k;
	socklen_t want;
	socklen_t have = offsetof(struct sockaddr_un, sun_path);

	if (copy_from_user(&want, addrlen, sizeof(want)) != 0)
		return -EFAULT;
	if ((int)want < 0)
		return -EINVAL;
	memset(&k, 0, sizeof(k));
	k.sun.sun_family = AF_UNIX;
	if (ua)
	{
		memcpy(k.sun.sun_path, ua->sun_path, sizeof(ua->sun_path));
		have = (socklen_t)ua->sun_len; /* 0: a datagram's unbound sender */
	}
	if (want > have)
		want = have;
	if (want > 0 && copy_to_user(addr, &k, want) != 0)
		return -EFAULT;
	if (copy_to_user(addrlen, &have, sizeof(have)) != 0)
		return -EFAULT;
	return 0;
}

/*
 * unix_find_bsd(): a pathname must exist (ENOENT) and be a socket
 * (ECONNREFUSED) before the socket bound to it is looked up.
 */
static int unix_find_check(const struct sock_unix_addr *ua)
{
	stat_t st;
	int rc;

	if (ua->is_abstract)
		return 0;
	rc = ir0_stat_path_routed(ua->key, &st);
	if (rc != 0)
		return rc;
	return S_ISSOCK(st.st_mode) ? 0 : -ECONNREFUSED;
}

/*
 * unix_bind_bsd(): the socket must be unbound (EINVAL), the parent
 * directory must exist, and the name must be free (EADDRINUSE).
 */
static int unix_bind_path_check(struct sock_stream *ss, const char *path)
{
	char parent[SOCK_UNIX_PATH_MAX];
	stat_t st;
	char *slash;
	int rc;

	if (sock_stream_unix_addr(ss))
		return -EINVAL;
	memcpy(parent, path, strlen(path) + 1);
	slash = strrchr(parent, '/');
	if (!slash)
		return -ENOENT;
	if (slash == parent)
		slash++;
	*slash = '\0';
	rc = ir0_stat_path_routed(parent, &st);
	if (rc != 0)
		return rc;
	if (!S_ISDIR(st.st_mode))
		return -ENOTDIR;
	if (ir0_stat_path_routed(path, &st) == 0)
		return -EADDRINUSE;
	return 0;
}

static int sock_alloc_fd_flags(void *sock, int is_stream, int type_flags)
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
		else if (sock_icmp_is(sock))
			sock_icmp_release(sock);
		else
			sock_udp_release(sock);
		return -EMFILE;
	}

	memset(&fd_table[fd], 0, sizeof(fd_table[fd]));
	fd_table[fd].in_use = true;
	fd_table[fd].vfs_file = sock;
	fd_table[fd].is_socket = true;
	if (type_flags & SOCK_CLOEXEC)
		fd_table[fd].fd_flags = FD_CLOEXEC;
	if (type_flags & SOCK_NONBLOCK)
		fd_table[fd].flags = O_NONBLOCK;
	fd_slot_note_created(&fd_table[fd]);
	return fd;
}

/*
 * net/socket.c sockfd_lookup(): a descriptor that is not open is EBADF,
 * one that is open but not a socket is ENOTSOCK.
 */
static int sock_fd_check(int fd)
{
	fd_entry_t *fd_table;

	if (!current_process || fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;
	fd_table = get_process_fd_table();
	if (!fd_table[fd].in_use)
		return -EBADF;
	if (!fd_table[fd].is_socket)
		return -ENOTSOCK;
	return 0;
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
	if (sock_icmp_is(p))
		return NULL;
	return (struct sock_udp *)p;
}

static struct sock_icmp *sock_icmp_fd_lookup(int fd)
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
	if (!sock_icmp_is(p))
		return NULL;
	return (struct sock_icmp *)p;
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

/*
 * The destination of an AF_UNIX datagram: none (the connected peer) when
 * the caller gave no address, else the named socket, which must exist.
 */
static int unix_dgram_dest(const struct sockaddr *uaddr, socklen_t len,
			   struct sock_unix_addr *ua,
			   const struct sock_unix_addr **dest)
{
	union sock_kaddr k;
	int err;

	*dest = NULL;
	if (!uaddr || len == 0)
		return 0;
	err = sock_addr_copy(&k, uaddr, len);
	if (err < 0)
		return err;
	err = unix_addr_get(&k, len, ua);
	if (err < 0)
		return err;
	err = unix_find_check(ua);
	if (err < 0)
		return err;
	*dest = ua;
	return 0;
}

/*
 * Queue one datagram from the kernel buffer @kbuf, waiting while the
 * receiver's queue is full unless the socket or @flags say not to.
 */
static ssize_t unix_dgram_send(int fd, struct sock_stream *ss,
			       const struct sock_unix_addr *dest,
			       const void *kbuf, size_t len, int flags)
{
	ssize_t n;

	for (;;)
	{
		n = sock_stream_sendto(ss, dest, kbuf, len);
		if (n != -EAGAIN)
			return n;
		if (sock_nonblock_fd(fd, flags))
			return -EAGAIN;
		if (signal_pending(current_process))
			return -EINTR;
		{
			int64_t sleep_ret = syscall_sleep_ms_locked(20);

			if (sleep_ret < 0)
				return sleep_ret;
		}
		ss = sock_stream_fd_lookup(fd);
		if (!ss)
			return -EBADF;
	}
}

/*
 * Kernel bounce buffer for a send/recv payload. A zero length is a valid
 * call (Linux: a stream reports 0, a datagram socket sends an empty
 * datagram), so it still gets a buffer: kmalloc(0) returns NULL.
 */
static uint8_t *sock_bounce_alloc(size_t len)
{
	return kmalloc(len ? len : 1);
}

/*
 * The checks socket(2) and socketpair(2) share, in Linux order: the flag
 * bits (__sys_socket), the family and the type range (__sock_create), then
 * the family's protocol and type (unix_create, inet_create).
 */
static int sock_create_check(int domain, int type, int protocol)
{
	int base = type & SOCK_TYPE_MASK;

	if (type & ~(SOCK_TYPE_MASK | SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	if (domain != AF_UNIX && domain != AF_INET)
		return -EAFNOSUPPORT;
	if (base >= SOCK_MAX)
		return -EINVAL;
	if (domain == AF_UNIX)
	{
		if (protocol != 0 && protocol != AF_UNIX)
			return -EPROTONOSUPPORT;
		if (base != SOCK_STREAM && base != SOCK_DGRAM)
			return -ESOCKTNOSUPPORT;
		return 0;
	}
	switch (base)
	{
	case SOCK_STREAM:
		return protocol == 0 || protocol == IPPROTO_TCP ?
			0 : -EPROTONOSUPPORT;
	case SOCK_DGRAM:
		return protocol == 0 || protocol == IPPROTO_UDP ?
			0 : -EPROTONOSUPPORT;
	case SOCK_RAW:
		return protocol == 0 || protocol == IPPROTO_ICMP ?
			0 : -EPROTONOSUPPORT;
	default:
		return -ESOCKTNOSUPPORT;
	}
}

int64_t sys_socket(int domain, int type, int protocol)
{
	int base = type & SOCK_TYPE_MASK;
	int ret;

#if !CONFIG_ENABLE_NETWORKING
	(void)domain;
	(void)type;
	(void)protocol;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	ret = sock_create_check(domain, type, protocol);
	if (ret < 0)
		return ret;

	if (domain == AF_UNIX || base == SOCK_STREAM)
	{
		struct sock_stream *ss;

		ss = sock_stream_create(domain == AF_UNIX ? IR0_AF_UNIX : IR0_AF_INET,
					base == SOCK_STREAM ? IR0_SOCK_STREAM :
							      IR0_SOCK_DGRAM);
		if (!ss)
			return -ENOMEM;
		return sock_alloc_fd_flags(ss, 1, type);
	}

	if (base == SOCK_RAW)
	{
		struct sock_icmp *icmp = sock_icmp_create();

		if (!icmp)
			return -ENOMEM;
		return sock_alloc_fd_flags(icmp, 0, type);
	}

	{
		struct sock_udp *sock = sock_udp_create();

		if (!sock)
			return -ENOMEM;
		return sock_alloc_fd_flags(sock, 0, type);
	}
}

int64_t sys_bind(int fd, const struct sockaddr *addr, socklen_t addrlen)
{
	union sock_kaddr k;
	struct sock_stream *ss;
	struct sock_icmp *icmp;
	struct sock_udp *sock;
	int err;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	err = sock_addr_copy(&k, addr, addrlen);
	if (err < 0)
		return err;

	ss = sock_stream_fd_lookup(fd);
	if (ss && sock_stream_family(ss) == IR0_AF_UNIX)
	{
		struct sock_unix_addr ua;

		/* An empty name would autobind on Linux: not supported. */
		if (addrlen <= offsetof(struct sockaddr_un, sun_path))
			return -EINVAL;
		err = unix_addr_get(&k, addrlen, &ua);
		if (err < 0)
			return err;
		if (ua.is_abstract)
			return sock_stream_bind_unix(ss, &ua);
		err = unix_bind_path_check(ss, ua.key);
		if (err < 0)
			return err;
		err = named_socket_create(ua.key, 0777);
		if (err < 0)
			return err;
		err = sock_stream_bind_unix(ss, &ua);
		if (err < 0)
			(void)named_socket_unlink(ua.key);
		return err;
	}

	/* inet_bind(): a short address is EINVAL, another family EAFNOSUPPORT. */
	if (addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;
	if (k.sin.sin_family != AF_INET)
		return -EAFNOSUPPORT;
	if (ss)
		return sock_stream_bind_inet(ss, ntohs(k.sin.sin_port));
	icmp = sock_icmp_fd_lookup(fd);
	if (icmp)
		return sock_icmp_bind(icmp, k.sin.sin_addr);
	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;
	return sock_udp_bind(sock, ntohs(k.sin.sin_port));
}

ssize_t sys_sendto(int fd, const void *buf, size_t len, int flags,
		   const struct sockaddr *dest_addr, socklen_t addrlen)
{
	int err;
	struct sock_stream *ss;
	union sock_kaddr k;
	uint16_t port;
	struct sock_udp *sock;
	struct sock_icmp *icmp;
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

	/* Linux import_ubuf(): a zero length needs no buffer, NULL included. */
	if (len && !access_ok(buf, len))
		return -EFAULT;

	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (ss && sock_stream_type(ss) == IR0_SOCK_DGRAM)
	{
		struct sock_unix_addr ua;
		const struct sock_unix_addr *dest;
		ssize_t n;

		err = unix_dgram_dest(dest_addr, addrlen, &ua, &dest);
		if (err < 0)
			return err;
		kbuf = sock_bounce_alloc(len);
		if (!kbuf)
			return -ENOMEM;
		n = copy_from_user(kbuf, buf, len) != 0 ? -EFAULT :
			unix_dgram_send(fd, ss, dest, kbuf, len, flags);
		kfree(kbuf);
		return n;
	}
	if (ss)
	{
		ssize_t n;

		kbuf = sock_bounce_alloc(len);
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

	/* udp_sendmsg(): with no address the connected peer is the target. */
	memset(&k, 0, sizeof(k));
	if (dest_addr)
	{
		err = sock_addr_copy(&k, dest_addr, addrlen);
		if (err < 0)
			return err;
		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (k.sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
	}

	icmp = sock_icmp_fd_lookup(fd);
	if (icmp)
	{
		if (!dest_addr)
			return -EDESTADDRREQ;
		kbuf = sock_bounce_alloc(len);
		if (!kbuf)
			return -ENOMEM;
		if (copy_from_user(kbuf, buf, len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		ret = sock_icmp_sendto(icmp, k.sin.sin_addr, kbuf, len);
		kfree(kbuf);
		if (ret < 0)
			return ret;
		(void)flags;
		return (ssize_t)ret;
	}

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;
	if (!dest_addr)
	{
		err = sock_udp_peer(sock, &k.sin.sin_addr, &port);
		if (err < 0)
			return err;
	}
	else
		port = ntohs(k.sin.sin_port);

	kbuf = sock_bounce_alloc(len);
	if (!kbuf)
		return -ENOMEM;
	if (copy_from_user(kbuf, buf, len) != 0)
	{
		kfree(kbuf);
		return -EFAULT;
	}

	ret = sock_udp_sendto(sock, k.sin.sin_addr, port, kbuf, len);
	kfree(kbuf);
	if (ret < 0)
		return ret;
	(void)flags;
	return (ssize_t)ret;
}

ssize_t sys_recvfrom(int fd, void *buf, size_t len, int flags,
		     struct sockaddr *src_addr, socklen_t *addrlen)
{
	int err;
	struct sock_stream *ss;
	struct sock_udp *sock;
	struct sock_icmp *icmp;
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

	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (ss)
	{
		struct sock_unix_addr from;
		uint64_t deadline = 0;
		uint64_t rto;

		/* Dequeues only what reached @buf: a bad @buf keeps the bytes. */
		nb = sock_nonblock_fd(fd, flags);
		rto = sock_stream_get_timeout_ms(ss, 1);
		if (rto > 0)
			deadline = clock_get_uptime_milliseconds() + rto;
		for (;;)
		{
			n = sock_stream_recv_user(ss, buf, len, flags, &from);
			if (n != -EAGAIN)
				break;
			if (nb)
				return -EAGAIN;
			if (deadline && clock_get_uptime_milliseconds() >= deadline)
				return -EAGAIN; /* Linux SO_RCVTIMEO → EAGAIN */
			if (signal_pending(current_process))
				return -EINTR;
			{
				int64_t sleep_ret = syscall_sleep_ms_locked(20);

				if (sleep_ret < 0)
					return sleep_ret;
			}
			ss = sock_stream_fd_lookup(fd);
			if (!ss)
				return -EBADF;
		}
		/* unix_dgram_recvmsg(): the sender's name, empty if unbound. */
		if (n >= 0 && src_addr && addrlen &&
		    sock_stream_type(ss) == IR0_SOCK_DGRAM)
		{
			err = (int)unix_fill_sockaddr(&from, src_addr, addrlen);
			if (err < 0)
				return err;
		}
		return n;
	}

	/*
	 * Linux import_ubuf(): the UDP/ICMP backends below dequeue before the
	 * copy, so a buffer outside userspace is EFAULT before a datagram is
	 * taken. The source address is written after the receive, and a
	 * fault there loses the datagram, as in Linux move_addr_to_user().
	 */
	if (len && !access_ok(buf, len))
		return -EFAULT;

	if (addrlen && copy_from_user(&out_len, addrlen, sizeof(socklen_t)) != 0)
		return -EFAULT;

	icmp = sock_icmp_fd_lookup(fd);
	if (icmp)
	{
		nb = sock_nonblock_fd(fd, flags);
		kbuf = sock_bounce_alloc(len);
		if (!kbuf)
			return -ENOMEM;

		for (;;)
		{
			n = sock_icmp_recvfrom(icmp, kbuf, len, MSG_DONTWAIT,
					       &src_ip_be);
			if (n != -EAGAIN)
				break;
			if (nb)
			{
				kfree(kbuf);
				return -EAGAIN;
			}
			if (signal_pending(current_process))
			{
				kfree(kbuf);
				return -EINTR;
			}
			{
				int64_t sleep_ret = syscall_sleep_ms_locked(20);

				if (sleep_ret < 0)
				{
					kfree(kbuf);
					return sleep_ret;
				}
			}
			if (signal_pending(current_process))
			{
				kfree(kbuf);
				return -EINTR;
			}
			icmp = sock_icmp_fd_lookup(fd);
			if (!icmp)
			{
				kfree(kbuf);
				return -EBADF;
			}
		}
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

	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;

	nb = sock_nonblock_fd(fd, flags);
	kbuf = sock_bounce_alloc(len);
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
	int err;
	union sock_kaddr k;
	struct sock_stream *ss;
	struct sock_udp *sock;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	err = sock_addr_copy(&k, addr, addrlen);
	if (err < 0)
		return err;
	if (addrlen < 2)
		return -EINVAL;

	ss = sock_stream_fd_lookup(fd);
	if (ss && sock_stream_family(ss) == IR0_AF_UNIX)
	{
		struct sock_unix_addr ua;

		err = unix_addr_get(&k, addrlen, &ua);
		if (err < 0)
			return err;
		err = unix_find_check(&ua);
		if (err < 0)
			return err;
		return sock_stream_connect_unix(ss, &ua);
	}
	if (ss)
	{
		if (addrlen < sizeof(struct sockaddr_in))
			return -EINVAL;
		if (k.sin.sin_family != AF_INET)
			return -EAFNOSUPPORT;
		return sock_stream_connect_inet_flags(ss, k.sin.sin_addr,
						      ntohs(k.sin.sin_port),
						      sock_nonblock_fd(fd, 0));
	}

	if (addrlen < sizeof(struct sockaddr_in))
		return -EINVAL;
	if (k.sin.sin_family != AF_INET)
		return -EAFNOSUPPORT;
	sock = sock_fd_lookup(fd);
	if (!sock)
		return -ENOTSOCK;
	return sock_udp_connect(sock, k.sin.sin_addr, ntohs(k.sin.sin_port));
}

int64_t sys_listen(int fd, int backlog)
{
	int err;
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)backlog;
	return -ENOSYS;
#endif
	if (!current_process)
		return -ESRCH;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return sock_stream_listen(ss, backlog);
}

int64_t sys_accept(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	return sys_accept4(fd, addr, addrlen, 0);
}

int64_t sys_accept4(int fd, struct sockaddr *addr, socklen_t *addrlen, int flags)
{
	int err;
	struct sock_stream *ss;
	struct sock_stream *child;
	int nfd;

	(void)addr;
	(void)addrlen;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)flags;
	return -ENOSYS;
#endif

	if (!current_process)
		return -ESRCH;
	if (flags & ~(SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;

	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
	{
		if (!sock_fd_lookup(fd))
			return -ENOTSOCK;
		return -EOPNOTSUPP;
	}

	/*
	 * Linux accept(2) blocks until a connection is pending unless the
	 * listener is O_NONBLOCK / accept4(..., SOCK_NONBLOCK). IR0 previously
	 * returned -EAGAIN immediately (same-process smokes connect-before-accept).
	 * X11 servers need the blocking path across fork/exec.
	 */
	for (;;)
	{
		child = sock_stream_accept(ss);
		if (child)
			break;
		if (sock_nonblock_fd(fd, 0) || (flags & SOCK_NONBLOCK))
			return -EAGAIN;
		if (signal_pending(current_process))
			return -EINTR;
#if CONFIG_ENABLE_NETWORKING
		net_stack_poll();
#endif
		{
			int64_t sleep_ret = syscall_sleep_ms_locked(20);

			if (sleep_ret < 0)
				return sleep_ret;
		}
#if CONFIG_ENABLE_NETWORKING
		net_stack_poll();
#endif
		ss = sock_stream_fd_lookup(fd);
		if (!ss)
			return -EBADF;
	}
	nfd = sock_alloc_fd_flags(child, 1, flags);
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
	if (type & ~(SOCK_TYPE_MASK | SOCK_CLOEXEC | SOCK_NONBLOCK))
		return -EINVAL;
	if (!sv)
		return -EFAULT;
	ret = sock_create_check(domain, type, protocol);
	if (ret < 0)
		return ret;
	/* inet_stream_ops and inet_dgram_ops have no socketpair. */
	if (domain != AF_UNIX)
		return -EOPNOTSUPP;
	ret = sock_stream_socketpair((type & SOCK_TYPE_MASK) == SOCK_STREAM ?
				     IR0_SOCK_STREAM : IR0_SOCK_DGRAM, &a, &b);
	if (ret < 0)
		return ret;

	fd0 = sock_alloc_fd_flags(a, 1, type);
	if (fd0 < 0)
	{
		sock_stream_release(b);
		return fd0;
	}
	fd1 = sock_alloc_fd_flags(b, 1, type);
	if (fd1 < 0)
	{
		(void)sys_close(fd0);
		return fd1;
	}

	sv_k[0] = fd0;
	sv_k[1] = fd1;
	if (copy_to_user(sv, sv_k, sizeof(sv_k)) != 0)
	{
		(void)sys_close(fd0);
		(void)sys_close(fd1);
		return -EFAULT;
	}
	return 0;
}

static void scm_rights_dtor(void *entry, size_t sz)
{
	fd_entry_t *e = (fd_entry_t *)entry;

	(void)sz;
	if (!e || !e->in_use)
		return;
	if (e->is_pipe && e->vfs_file)
		pipe_fd_entry_release_refs((pipe_t *)e->vfs_file, e);
	else if (e->is_socket && e->vfs_file)
	{
		if (sock_stream_is(e->vfs_file))
			sock_stream_release((struct sock_stream *)e->vfs_file);
		else if (sock_icmp_is(e->vfs_file))
			sock_icmp_release((struct sock_icmp *)e->vfs_file);
		else if (!sock_stream_is_slot(e->vfs_file))
			sock_udp_release((struct sock_udp *)e->vfs_file);
	}
	else if (e->is_devfs)
	{
		devfs_node_t *node = fd_entry_devfs_node(e);

		if (e->vfs_file &&
		    devfs_is_ptmx_device(e->dev_device_id))
		{
			devfs_pty_master_release_vfs(e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->vfs_file &&
		    devfs_node_wants_text_snap(e->dev_device_id))
		{
			devfs_text_snap_release((devfs_text_snap_t *)e->vfs_file);
			e->vfs_file = NULL;
		}
		if (node)
			devfs_close_node(node);
	}
	else if (e->is_memfd && e->vfs_file)
		ir0_memfd_release((struct ir0_memfd *)e->vfs_file);
	else if (e->vfs_file)
		vfs_close((struct vfs_file *)e->vfs_file);
	memset(e, 0, sizeof(*e));
}

static void scm_rights_ensure_dtor(void)
{
	static int once;

	if (!once)
	{
		sock_stream_set_rights_dtor(scm_rights_dtor);
		once = 1;
	}
}

static int scm_clone_fd_entry(fd_entry_t *dst, int srcfd)
{
	fd_entry_t *tab = get_process_fd_table();

	if (!tab || srcfd < 0 || srcfd >= MAX_FDS_PER_PROCESS || !tab[srcfd].in_use)
		return -EBADF;
	*dst = tab[srcfd];
	dst->fd_flags = 0;
	if (dst->is_pipe && dst->vfs_file)
		pipe_fd_entry_acquire_refs(dst);
	else if (dst->is_socket && dst->vfs_file)
	{
		if (sock_stream_is(dst->vfs_file))
			sock_stream_acquire((struct sock_stream *)dst->vfs_file);
		else if (sock_icmp_is(dst->vfs_file))
			sock_icmp_acquire((struct sock_icmp *)dst->vfs_file);
		else if (!sock_stream_is_slot(dst->vfs_file))
			sock_udp_acquire((struct sock_udp *)dst->vfs_file);
	}
	else if (dst->is_devfs)
	{
		devfs_node_t *node = fd_entry_devfs_node(dst);

		if (node)
			node->ref_count++;
		if (dst->vfs_file &&
		    devfs_is_ptmx_device(dst->dev_device_id))
			devfs_pty_master_dup_vfs(dst->vfs_file);
		else if (devfs_is_pts_device(dst->dev_device_id))
			devfs_pty_slave_dup_device(dst->dev_device_id);
		else if (dst->vfs_file &&
		    devfs_node_wants_text_snap(dst->dev_device_id))
			devfs_text_snap_acquire((devfs_text_snap_t *)dst->vfs_file);
	}
	else if (dst->is_pseudo && dst->vfs_file)
	{
		pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)dst->vfs_file;

		pseudo_fd_bind_acquire(bind);
	}
	else if (dst->is_memfd && dst->vfs_file)
		ir0_memfd_acquire((struct ir0_memfd *)dst->vfs_file);
	else if (dst->vfs_file)
		vfs_file_acquire((struct vfs_file *)dst->vfs_file);
	return 0;
}

static int scm_install_fd_entry(const fd_entry_t *src)
{
	fd_entry_t *tab = get_process_fd_table();
	int fd;

	if (!tab || !src)
		return -EINVAL;
	for (fd = 3; fd < MAX_FDS_PER_PROCESS; fd++)
	{
		if (!tab[fd].in_use)
			break;
	}
	if (fd >= MAX_FDS_PER_PROCESS)
		return -EMFILE;
	tab[fd] = *src;
	tab[fd].in_use = true;
	fd_slot_note_created(&tab[fd]);
	return fd;
}

static void scm_close_installed_fds(int *fds, size_t count)
{
	while (count > 0)
	{
		count--;
		(void)sys_close(fds[count]);
	}
}

static void scm_discard_staged(fd_entry_t *entries, size_t count)
{
	while (count > 0)
	{
		count--;
		scm_rights_dtor(&entries[count], sizeof(entries[count]));
	}
}

static int scm_stage_rights(struct sock_stream *peer, const uint8_t *ctrl,
			    size_t ctrl_len, fd_entry_t *entries,
			    size_t *entry_count)
{
	size_t off = 0;
	size_t count = 0;

	while (off + sizeof(struct cmsghdr) <= ctrl_len)
	{
		const struct cmsghdr *cmsg = (const struct cmsghdr *)(ctrl + off);
		size_t payload;
		const int *fds;
		size_t nfd;
		size_t fi;

		if (cmsg->cmsg_len < IR0_CMSG_ALIGN(sizeof(struct cmsghdr)) ||
		    cmsg->cmsg_len > ctrl_len - off)
			goto invalid;
		if (cmsg->cmsg_level == SOL_SOCKET)
		{
			if (cmsg->cmsg_type != SCM_RIGHTS)
				goto invalid;
			payload = cmsg->cmsg_len -
				  IR0_CMSG_ALIGN(sizeof(struct cmsghdr));
			if (payload == 0 || payload % sizeof(int) != 0)
				goto invalid;
			nfd = payload / sizeof(int);
			if (nfd > SOCK_STREAM_RIGHTS_MAX - count)
				goto invalid;
			fds = (const int *)IR0_CMSG_DATA(cmsg);
			for (fi = 0; fi < nfd; fi++)
			{
				int ret = scm_clone_fd_entry(&entries[count], fds[fi]);

				if (ret < 0)
				{
					scm_discard_staged(entries, count);
					return ret;
				}
				count++;
			}
		}
		off += IR0_CMSG_ALIGN(cmsg->cmsg_len);
	}
	/* Linux accepts CMSG_LEN() without trailing CMSG_SPACE() padding. */
	if (count > (size_t)(SOCK_STREAM_RIGHTS_MAX -
			     sock_stream_rights_count(peer)))
		goto no_buffer;
	*entry_count = count;
	return 0;

invalid:
	scm_discard_staged(entries, count);
	return -EINVAL;
no_buffer:
	scm_discard_staged(entries, count);
	return -ENOBUFS;
}

/* Copy in a msghdr's iovec array and add up its length. */
static int unix_dgram_iov(const struct msghdr *msg, struct iovec *iov,
			  size_t *total)
{
	size_t i;

	*total = 0;
	if (msg->msg_iovlen > 8)
		return -EMSGSIZE;
	if (msg->msg_iovlen &&
	    (!msg->msg_iov ||
	     copy_from_user(iov, msg->msg_iov,
			    msg->msg_iovlen * sizeof(*iov)) != 0))
		return -EFAULT;
	for (i = 0; i < msg->msg_iovlen; i++)
	{
		if (iov[i].iov_len > SIZE_MAX / 2 - *total)
			return -EINVAL;
		*total += iov[i].iov_len;
	}
	return 0;
}

/*
 * sendmsg(2) on an AF_UNIX datagram socket: the iovecs gathered into one
 * datagram for msg_name, or for the peer. Ancillary data is not supported
 * on datagram sockets yet (EOPNOTSUPP).
 */
static ssize_t unix_dgram_sendmsg(int fd, struct sock_stream *ss,
				  const struct msghdr *msg, int flags)
{
	struct iovec iov[8];
	struct sock_unix_addr ua;
	const struct sock_unix_addr *dest;
	uint8_t *kbuf;
	size_t total;
	size_t off = 0;
	size_t i;
	ssize_t n;
	int err;

	if (msg->msg_controllen > 0)
		return -EOPNOTSUPP;
	err = unix_dgram_iov(msg, iov, &total);
	if (err < 0)
		return err;
	err = unix_dgram_dest(msg->msg_name, msg->msg_namelen, &ua, &dest);
	if (err < 0)
		return err;
	kbuf = sock_bounce_alloc(total);
	if (!kbuf)
		return -ENOMEM;
	for (i = 0; i < msg->msg_iovlen; i++)
	{
		if (iov[i].iov_len &&
		    copy_from_user(kbuf + off, iov[i].iov_base, iov[i].iov_len) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		off += iov[i].iov_len;
	}
	n = unix_dgram_send(fd, ss, dest, kbuf, total, flags);
	kfree(kbuf);
	return n;
}

/*
 * recvmsg(2) on an AF_UNIX datagram socket: one datagram scattered over
 * the iovecs, the sender in msg_name and MSG_TRUNC in msg_flags when the
 * iovecs were too short (unix_dgram_recvmsg()).
 */
static ssize_t unix_dgram_recvmsg(int fd, struct sock_stream *ss,
				  struct msghdr *umsg, struct msghdr *msg,
				  int flags)
{
	struct iovec iov[8];
	struct sock_unix_addr from;
	union sock_kaddr k;
	uint8_t *kbuf;
	size_t total;
	size_t copied;
	size_t off = 0;
	size_t i;
	ssize_t n;
	int err;

	err = unix_dgram_iov(msg, iov, &total);
	if (err < 0)
		return err;
	kbuf = sock_bounce_alloc(total);
	if (!kbuf)
		return -ENOMEM;
	for (;;)
	{
		n = sock_stream_recv_flags(ss, kbuf, total, flags | MSG_TRUNC,
					   &from);
		if (n != -EAGAIN)
			break;
		if (sock_nonblock_fd(fd, flags))
			break;
		if (signal_pending(current_process))
		{
			n = -EINTR;
			break;
		}
		{
			int64_t sleep_ret = syscall_sleep_ms_locked(20);

			if (sleep_ret < 0)
			{
				n = sleep_ret;
				break;
			}
		}
		ss = sock_stream_fd_lookup(fd);
		if (!ss)
		{
			n = -EBADF;
			break;
		}
	}
	if (n < 0)
	{
		kfree(kbuf);
		return n;
	}
	copied = (size_t)n < total ? (size_t)n : total;
	for (i = 0; i < msg->msg_iovlen && off < copied; i++)
	{
		size_t take = copied - off;

		if (take > iov[i].iov_len)
			take = iov[i].iov_len;
		if (take && copy_to_user(iov[i].iov_base, kbuf + off, take) != 0)
		{
			kfree(kbuf);
			return -EFAULT;
		}
		off += take;
	}
	kfree(kbuf);
	if (msg->msg_name)
	{
		size_t want = msg->msg_namelen;

		memset(&k, 0, sizeof(k));
		k.sun.sun_family = AF_UNIX;
		memcpy(k.sun.sun_path, from.sun_path, sizeof(from.sun_path));
		if (want > from.sun_len)
			want = from.sun_len;
		if (want && copy_to_user(msg->msg_name, &k, want) != 0)
			return -EFAULT;
		msg->msg_namelen = (socklen_t)from.sun_len;
	}
	msg->msg_controllen = 0;
	msg->msg_flags = (size_t)n > total ? MSG_TRUNC : 0;
	if (copy_to_user(umsg, msg, sizeof(*msg)) != 0)
		return -EFAULT;
	return (flags & MSG_TRUNC) ? n : (ssize_t)copied;
}

ssize_t sys_sendmsg(int fd, const struct msghdr *umsg, int flags)
{
	int err;
	struct msghdr msg;
	struct sock_stream *ss;
	struct sock_stream *peer;
	uint8_t ctrl[256];
	struct iovec iov_stack[8];
	fd_entry_t staged_rights[SOCK_STREAM_RIGHTS_MAX];
	size_t staged_count = 0;
	uint8_t *kbuf = NULL;
	size_t len = 0;
	size_t iovlen;
	size_t i;
	ssize_t total = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)umsg;
	(void)flags;
	return -ENOSYS;
#endif
	scm_rights_ensure_dtor();
	(void)flags;
	if (!current_process || !umsg)
		return -EFAULT;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	if (copy_from_user(&msg, umsg, sizeof(msg)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	if (sock_stream_type(ss) == IR0_SOCK_DGRAM)
		return unix_dgram_sendmsg(fd, ss, &msg, flags);
	peer = sock_stream_get_peer(ss);
	if (!peer)
		return -ENOTCONN;
	iovlen = msg.msg_iovlen;
	if (iovlen > 8)
		return -EMSGSIZE;
	if (iovlen > 0)
	{
		if (!msg.msg_iov ||
		    copy_from_user(iov_stack, msg.msg_iov, iovlen * sizeof(struct iovec)) != 0)
			return -EFAULT;
	}
	/*
	 * Linux import_iovec(): a buffer outside userspace is EFAULT before
	 * anything else is looked at.
	 */
	for (i = 0; i < iovlen; i++)
	{
		if (iov_stack[i].iov_len == 0)
			continue;
		if (!iov_stack[i].iov_base ||
		    !access_ok(iov_stack[i].iov_base, iov_stack[i].iov_len))
			return -EFAULT;
		if (iov_stack[i].iov_len > SIZE_MAX - len)
			return -EINVAL;
		len += iov_stack[i].iov_len;
	}
	/*
	 * unix_stream_sendmsg(): the payload is copied in before anything is
	 * queued, so a faulting iovec sends nothing.
	 */
	if (len > 0)
	{
		size_t off = 0;

		kbuf = kmalloc(len);
		if (!kbuf)
			return -ENOMEM;
		for (i = 0; i < iovlen; i++)
		{
			if (iov_stack[i].iov_len == 0)
				continue;
			if (copy_from_user(kbuf + off, iov_stack[i].iov_base,
					   iov_stack[i].iov_len) != 0)
			{
				kfree(kbuf);
				return -EFAULT;
			}
			off += iov_stack[i].iov_len;
		}
	}
	if (msg.msg_controllen > 0)
	{
		int ret;

		if (msg.msg_controllen > sizeof(ctrl) || !msg.msg_control)
			ret = -ENOBUFS;
		else if (copy_from_user(ctrl, msg.msg_control,
					msg.msg_controllen) != 0)
			ret = -EFAULT;
		else
		{
			memset(staged_rights, 0, sizeof(staged_rights));
			ret = scm_stage_rights(peer, ctrl, msg.msg_controllen,
					       staged_rights, &staged_count);
		}
		if (ret < 0)
		{
			kfree(kbuf);
			return ret;
		}
	}
	if (len > 0)
	{
		total = sock_stream_send(ss, kbuf, len);
		kfree(kbuf);
		if (total < 0)
		{
			scm_discard_staged(staged_rights, staged_count);
			return total;
		}
	}
	if (staged_count > 0)
	{
		int ret = sock_stream_rights_push_batch(peer, staged_rights,
							staged_count,
							sizeof(staged_rights[0]));

		if (ret < 0)
		{
			scm_discard_staged(staged_rights, staged_count);
			return ret;
		}
	}
	return total;
}

ssize_t sys_recvmsg(int fd, struct msghdr *umsg, int flags)
{
	int err;
	struct msghdr msg;
	struct sock_stream *ss;
	struct iovec iov_stack[8];
	size_t iovlen;
	size_t i;
	ssize_t total = 0;
	uint8_t ctrl[256];
	size_t ctrl_used = 0;
	size_t rights = 0;
	int installed_fds[SOCK_STREAM_RIGHTS_MAX];
	size_t installed_fd_count = 0;
	int nonblock;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)umsg;
	(void)flags;
	return -ENOSYS;
#endif
	scm_rights_ensure_dtor();
	if (!current_process || !umsg)
		return -EFAULT;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	if (copy_from_user(&msg, umsg, sizeof(msg)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	if (sock_stream_type(ss) == IR0_SOCK_DGRAM)
		return unix_dgram_recvmsg(fd, ss, umsg, &msg, flags);
	nonblock = sock_nonblock_fd(fd, flags);
	iovlen = msg.msg_iovlen;
	if (iovlen > 8)
		return -EMSGSIZE;
	if (iovlen > 0)
	{
		if (!msg.msg_iov ||
		    copy_from_user(iov_stack, msg.msg_iov, iovlen * sizeof(struct iovec)) != 0)
			return -EFAULT;
	}
	/*
	 * Linux import_iovec(): a buffer outside userspace is EFAULT before
	 * the receive waits for data it could never take.
	 */
	for (i = 0; i < iovlen; i++)
	{
		if (iov_stack[i].iov_len != 0 &&
		    (!iov_stack[i].iov_base ||
		     !access_ok(iov_stack[i].iov_base,
					       iov_stack[i].iov_len)))
			return -EFAULT;
	}
	if (sock_stream_rights_count(ss) > 0 && msg.msg_control &&
	    msg.msg_controllen > 0)
	{
		size_t max_fit;

		rights = (size_t)sock_stream_rights_count(ss);
		if (rights > SOCK_STREAM_RIGHTS_MAX)
			rights = SOCK_STREAM_RIGHTS_MAX;
		max_fit = (msg.msg_controllen >= IR0_CMSG_SPACE(sizeof(int)))
			? ((msg.msg_controllen - IR0_CMSG_ALIGN(sizeof(struct cmsghdr))) /
			   sizeof(int))
			: 0;
		if (rights > max_fit)
			rights = max_fit;
		if (rights == 0 ||
		    IR0_CMSG_SPACE(rights * sizeof(int)) > sizeof(ctrl) ||
		    IR0_CMSG_SPACE(rights * sizeof(int)) > msg.msg_controllen)
			return -ENOBUFS;
	}

	/*
	 * unix_stream_read_generic(): the payload first, dequeued only once it
	 * reached userspace; a fault leaves data and rights queued.
	 */
	for (i = 0; i < iovlen; i++)
	{
		ssize_t n;

		if (!iov_stack[i].iov_base || iov_stack[i].iov_len == 0)
			continue;
		for (;;)
		{
			n = sock_stream_recv_user(ss, iov_stack[i].iov_base,
						  iov_stack[i].iov_len, flags, NULL);
			if (n != -EAGAIN)
				break;
			/* Linux may return an already received prefix immediately. */
			if (total > 0 || rights > 0 || nonblock)
				break;
			if (signal_pending(current_process))
				return -EINTR;
			{
				int64_t sleep_ret = syscall_sleep_ms_locked(20);

				if (sleep_ret < 0)
					return sleep_ret;
			}
			ss = sock_stream_fd_lookup(fd);
			if (!ss)
				return -EBADF;
		}
		if (n < 0)
		{
			if (total > 0 || (n == -EAGAIN && rights > 0))
				break;
			return n;
		}
		total += n;
		if ((size_t)n < iov_stack[i].iov_len)
			break;
	}

	/* Then the descriptors that rode with it (scm_recv_unix()). */
	if (rights > 0)
	{
		fd_entry_t ents[SOCK_STREAM_RIGHTS_MAX];
		struct cmsghdr *cmsg;
		int *fdp;
		size_t fi;

		for (fi = 0; fi < rights; fi++)
		{
			memset(&ents[fi], 0, sizeof(ents[fi]));
			if (sock_stream_rights_pop(ss, &ents[fi], sizeof(ents[fi])) != 0)
				break;
			installed_fds[fi] = scm_install_fd_entry(&ents[fi]);
			if (installed_fds[fi] < 0)
			{
				int err = installed_fds[fi];

				scm_rights_dtor(&ents[fi], sizeof(ents[fi]));
				scm_close_installed_fds(installed_fds,
							installed_fd_count);
				return err;
			}
			installed_fd_count++;
		}
		if (installed_fd_count > 0)
		{
			ctrl_used = IR0_CMSG_SPACE(installed_fd_count * sizeof(int));
			memset(ctrl, 0, ctrl_used);
			cmsg = (struct cmsghdr *)ctrl;
			cmsg->cmsg_len = IR0_CMSG_LEN(installed_fd_count * sizeof(int));
			cmsg->cmsg_level = SOL_SOCKET;
			cmsg->cmsg_type = SCM_RIGHTS;
			fdp = (int *)IR0_CMSG_DATA(cmsg);
			for (fi = 0; fi < installed_fd_count; fi++)
				fdp[fi] = installed_fds[fi];
		}
	}
	if (ctrl_used > 0)
	{
		if (copy_to_user(msg.msg_control, ctrl, ctrl_used) != 0)
		{
			scm_close_installed_fds(installed_fds,
						installed_fd_count);
			return -EFAULT;
		}
		msg.msg_controllen = ctrl_used;
		msg.msg_flags = 0;
		if (copy_to_user(umsg, &msg, sizeof(msg)) != 0)
		{
			scm_close_installed_fds(installed_fds,
						installed_fd_count);
			return -EFAULT;
		}
	}
	else if (msg.msg_control)
	{
		msg.msg_controllen = 0;
		if (copy_to_user(umsg, &msg, sizeof(msg)) != 0)
			return -EFAULT;
	}
	return total;
}

int64_t sys_shutdown(int fd, int how)
{
	int err;
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)how;
	return -ENOSYS;
#endif
	if (!current_process)
		return -ESRCH;
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	return sock_stream_shutdown(ss, how);
}

int64_t sys_getsockname(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err;
	struct sock_stream *ss;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (sock_stream_family(ss) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	return unix_fill_sockaddr(sock_stream_unix_addr(ss), addr, addrlen);
}

int64_t sys_getpeername(int fd, struct sockaddr *addr, socklen_t *addrlen)
{
	int err;
	struct sock_stream *ss;
	struct sock_stream *peer;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)addr;
	(void)addrlen;
	return -ENOSYS;
#endif
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	peer = sock_stream_get_peer(ss);
	if (!peer)
		return -ENOTCONN;
	if (sock_stream_family(peer) != IR0_AF_UNIX)
		return -EOPNOTSUPP;
	return unix_fill_sockaddr(sock_stream_unix_addr(peer), addr, addrlen);
}

int64_t sys_setsockopt(int fd, int level, int optname, const void *optval,
		       socklen_t optlen)
{
	int err;
	struct sock_stream *ss;
	struct sock_icmp *icmp;
	int val = 0;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)level;
	(void)optname;
	(void)optval;
	(void)optlen;
	return -ENOSYS;
#endif
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	ss = sock_stream_fd_lookup(fd);
	icmp = sock_icmp_fd_lookup(fd);

	if (icmp)
	{
		if (level == IPPROTO_IP && optname == IP_TTL)
		{
			if (optlen < sizeof(int) || !optval)
				return -EINVAL;
			if (copy_from_user(&val, optval, sizeof(val)) != 0)
				return -EFAULT;
			return sock_icmp_set_ttl(icmp, val);
		}
		if (level == IPPROTO_IP && optname == IP_MULTICAST_IF)
		{
			/*
			 * BusyBox ping -I <addr> passes sockaddr_in (16B). Linux
			 * treats optlen>=12 as ip_mreqn; imr_address sits at
			 * offset 4 — same as sin_addr in sockaddr_in.
			 */
			uint8_t buf[16];
			uint32_t addr = 0;
			size_t n;

			if (!optval || optlen < 4)
				return -EINVAL;
			n = optlen;
			if (n > sizeof(buf))
				n = sizeof(buf);
			memset(buf, 0, sizeof(buf));
			if (copy_from_user(buf, optval, n) != 0)
				return -EFAULT;
			if (n >= 8)
				memcpy(&addr, buf + 4, 4);
			else
				memcpy(&addr, buf, 4);
			return sock_icmp_bind(icmp, addr);
		}
		if (level == SOL_SOCKET && optname == SO_BINDTODEVICE)
		{
			char name[16];
			size_t n;
			size_t i;

			if (!optval || optlen == 0)
				return sock_icmp_set_bind_device(icmp, NULL, 0);
			/*
			 * Linux accepts a C string or struct ifreq (name at offset 0).
			 * BusyBox setsockopt_bindtodevice() passes sizeof(ifreq).
			 */
			n = optlen;
			if (n > sizeof(name))
				n = sizeof(name);
			memset(name, 0, sizeof(name));
			if (copy_from_user(name, optval, n) != 0)
				return -EFAULT;
			for (i = 0; i < n && name[i]; i++)
				;
			return sock_icmp_set_bind_device(icmp, name, i);
		}
		return -ENOPROTOOPT;
	}

	if (!ss)
		return -ENOTSOCK;
	if (level != SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname == SO_REUSEADDR)
	{
		if (optlen < sizeof(int) || !optval)
			return -EINVAL;
		if (copy_from_user(&val, optval, sizeof(val)) != 0)
			return -EFAULT;
		return sock_stream_set_reuseaddr(ss, val);
	}
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
	{
		struct timeval tv;
		uint64_t ms;

		if (optlen < sizeof(tv) || !optval)
			return -EINVAL;
		if (copy_from_user(&tv, optval, sizeof(tv)) != 0)
			return -EFAULT;
		if (tv.tv_sec < 0 || tv.tv_usec < 0 || tv.tv_usec >= 1000000)
			return -EINVAL;
		ms = (uint64_t)tv.tv_sec * 1000ULL +
		     (uint64_t)tv.tv_usec / 1000ULL;
		return sock_stream_set_timeout_ms(ss, optname == SO_RCVTIMEO, ms);
	}
	if (optname == SO_BINDTODEVICE)
	{
		/* Single-NIC stack: accept and ignore for STREAM. */
		return 0;
	}
	return -ENOPROTOOPT;
}

int64_t sys_getsockopt(int fd, int level, int optname, void *optval,
		       socklen_t *optlen)
{
	int err;
	struct sock_stream *ss;
	socklen_t len;
	int val;

#if !CONFIG_ENABLE_NETWORKING
	(void)fd;
	(void)level;
	(void)optname;
	(void)optval;
	(void)optlen;
	return -ENOSYS;
#endif
	err = sock_fd_check(fd);
	if (err < 0)
		return err;
	if (!optval || !optlen)
		return -EFAULT;
	if (copy_from_user(&len, optlen, sizeof(len)) != 0)
		return -EFAULT;
	ss = sock_stream_fd_lookup(fd);
	if (!ss)
		return -ENOTSOCK;
	if (level != SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname == SO_TYPE)
	{
		val = sock_stream_type(ss) == IR0_SOCK_DGRAM ? SOCK_DGRAM :
							      SOCK_STREAM;
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	if (optname == SO_ERROR)
	{
		val = sock_stream_take_so_error(ss);
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	if (optname == SO_REUSEADDR)
	{
		val = sock_stream_get_reuseaddr(ss);
		if (len < sizeof(val))
			return -EINVAL;
		if (copy_to_user(optval, &val, sizeof(val)) != 0)
			return -EFAULT;
		len = sizeof(val);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	if (optname == SO_RCVTIMEO || optname == SO_SNDTIMEO)
	{
		struct timeval tv;
		uint64_t ms = sock_stream_get_timeout_ms(ss, optname == SO_RCVTIMEO);

		if (len < sizeof(tv))
			return -EINVAL;
		tv.tv_sec = (long)(ms / 1000ULL);
		tv.tv_usec = (long)((ms % 1000ULL) * 1000ULL);
		if (copy_to_user(optval, &tv, sizeof(tv)) != 0)
			return -EFAULT;
		len = sizeof(tv);
		if (copy_to_user(optlen, &len, sizeof(len)) != 0)
			return -EFAULT;
		return 0;
	}
	return -ENOPROTOOPT;
}
