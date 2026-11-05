/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_rootfs_socket.c
 * Description: early AF_UNIX sockets, socketpairs and FIFOs.
 */

#include "early_rootfs_internal.h"

static int64_t unix_dgram_deposit(int uid, const uint8_t *tmp, unsigned count);

static int copy_sun_path(uint64_t addr, uint64_t addrlen, char *out,
			 unsigned cap)
{
	uint8_t raw[2 + IR0_EARLY_SUN_PATH];
	unsigned n;

	if (addrlen < 3 || addrlen > sizeof(raw))
		return 0;
	n = (unsigned)addrlen;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(raw, addr, n) != 0)
		return 0;
	return ir0_early_sockaddr_un_path(raw, n, out, cap);
}

static int copy_sun_abs(uint64_t addr, uint64_t addrlen, uint8_t *out,
			unsigned *len, unsigned cap)
{
	uint8_t raw[2 + IR0_EARLY_SUN_PATH];
	unsigned n;
	unsigned i;

	if (addrlen < 3 || addrlen > sizeof(raw) || !out || !len)
		return 0;
	n = (unsigned)addrlen;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(raw, addr, n) != 0)
		return 0;
	if (!ir0_early_sockaddr_un_is_abstract(raw, n))
		return 0;
	*len = ir0_early_sockaddr_un_abs_len(n);
	if (*len > cap)
		*len = cap;
	for (i = 0; i < *len; i++)
		out[i] = raw[2U + i];
	return 1;
}

static int unix_find_abstract(const uint8_t *name, unsigned len, int stream)
{
	int i;
	unsigned k;

	if (!name)
		return -1;
	for (i = 0; i < UNIX_MAX; i++)
	{
		if (!g_unix[i].used || !g_unix[i].abstract ||
		    g_unix[i].stream != stream || g_unix[i].abs_len != len)
			continue;
		for (k = 0; k < len; k++)
		{
			if (g_unix[i].abs_name[k] != name[k])
				break;
		}
		if (k == len)
			return i;
	}
	return -1;
}

void note_sender_creds_unix(int uid)
{
	if (uid < 0 || uid >= UNIX_MAX || !g_unix[uid].used)
		return;
	early_process_current_ucred(&g_unix[uid].cred_pid, &g_unix[uid].cred_uid,
				    &g_unix[uid].cred_gid);
	g_unix[uid].cred_valid = 1;
}

void note_sender_creds_sock(int pair, int dest_end)
{
	if (pair < 0 || pair >= SOCKPAIR_MAX || dest_end < 0 || dest_end > 1 ||
	    !g_sock[pair].used)
		return;
	early_process_current_ucred(&g_sock[pair].cred_pid[dest_end],
				    &g_sock[pair].cred_uid[dest_end],
				    &g_sock[pair].cred_gid[dest_end]);
	g_sock[pair].cred_valid[dest_end] = 1;
}

static int64_t write_sun_abs(uint64_t addr, uint64_t addrlenp,
			     const uint8_t *name, unsigned n)
{
	uint32_t want;
	uint8_t fam[2];
	unsigned i;

	if (!addrlenp)
		return 0;
	if (!g_user || !g_user->copy_to_user)
		return -EFAULT;
	want = 2U + n;
	if (g_user->copy_to_user(addrlenp, &want, 4) != 0)
		return -EFAULT;
	if (!addr)
		return 0;
	fam[0] = (uint8_t)IR0_EARLY_AF_UNIX;
	fam[1] = 0;
	if (g_user->copy_to_user(addr, fam, 2) != 0)
		return -EFAULT;
	for (i = 0; i < n && i < IR0_EARLY_SUN_PATH; i++)
	{
		if (g_user->copy_to_user(addr + 2 + i, &name[i], 1) != 0)
			return -EFAULT;
	}
	return 0;
}

static int64_t write_sun_name(uint64_t addr, uint64_t addrlenp,
			      const char *path)
{
	uint32_t want = 2;
	uint8_t fam[2];
	unsigned i;
	unsigned plen = 0;

	if (!addrlenp)
		return 0;
	if (!g_user || !g_user->copy_from_user || !g_user->copy_to_user)
		return -EFAULT;
	if (g_user->copy_from_user(&want, addrlenp, 4) != 0)
		return -EFAULT;
	if (!addr)
		return 0;
	if (path)
	{
		while (path[plen] != '\0')
			plen++;
		want = 2U + plen + 1U;
	}
	else
		want = 2;
	if (g_user->copy_to_user(addrlenp, &want, 4) != 0)
		return -EFAULT;
	fam[0] = (uint8_t)IR0_EARLY_AF_UNIX;
	fam[1] = 0;
	if (g_user->copy_to_user(addr, fam, 2) != 0)
		return -EFAULT;
	if (path)
	{
		for (i = 0; i <= plen && 2U + i < 2U + IR0_EARLY_SUN_PATH; i++)
		{
			uint8_t ch = (uint8_t)path[i];

			if (g_user->copy_to_user(addr + 2 + i, &ch, 1) != 0)
				return -EFAULT;
		}
	}
	return 0;
}

int64_t early_rootfs_socket(int domain, int type, int protocol)
{
	int uid;
	int fd;
	int idx;
	int kind = type & 0xf;

	if (!g_ready)
		return -ENOSYS;
	if (domain != IR0_EARLY_AF_UNIX)
		return -EAFNOSUPPORT;
	if (protocol != 0)
		return -EPROTONOSUPPORT;
	if (kind != IR0_EARLY_SOCK_STREAM && kind != IR0_EARLY_SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;
	if (type & ~(0xf | IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	for (uid = 0; uid < UNIX_MAX; uid++)
	{
		if (!g_unix[uid].used)
			break;
	}
	if (uid >= UNIX_MAX)
		return -EMFILE;
	fd = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (fd < 0)
		return fd;
	idx = slot_from_fd(fd);
	if (idx < 0)
	{
		close_fd(fd);
		return -EBADF;
	}
	g_unix[uid].used = 1;
	g_unix[uid].refs = 1;
	g_unix[uid].stream = (kind == IR0_EARLY_SOCK_STREAM);
	g_unix[uid].bound_fs = -1;
	g_unix[uid].listening = 0;
	g_unix[uid].pending_pair = -1;
	g_unix[uid].pending_end = 0;
	g_unix[uid].peer_fs = -1;
	g_unix[uid].reuseaddr = 0;
	g_unix[uid].so_error = 0;
	g_unix[uid].shut_rd = 0;
	g_unix[uid].shut_wr = 0;
	g_unix[uid].rights_n = 0;
	g_unix[uid].abstract = 0;
	g_unix[uid].abs_len = 0;
	g_unix[uid].peer_uid = -1;
	g_unix[uid].passcred = 0;
	g_unix[uid].cred_valid = 0;
	g_unix[uid].cred_explicit = 0;
	g_unix[uid].bound_path[0] = '\0';
	g_unix[uid].rpos = 0;
	g_unix[uid].len = 0;
	g_slots[idx].pipe_id = UNIX_ID_BASE + uid;
	set_flags_of_fd(fd, IR0_O_RDWR |
			    ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							       0));
	set_cloexec_of_fd(fd, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	return fd;
}

int64_t early_rootfs_sock_bind(int fd, uint64_t addr, uint64_t addrlen)
{
	char pbuf[RAMTAB_NAME];
	uint8_t abs[IR0_EARLY_SUN_PATH];
	unsigned alen = 0;
	int uid = unix_id_of_fd(fd);
	int ret;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (g_unix[uid].bound_fs >= 0 || g_unix[uid].abstract)
		return -EINVAL;
	if (copy_sun_abs(addr, addrlen, abs, &alen, sizeof(abs)))
	{
		if (unix_find_abstract(abs, alen, g_unix[uid].stream) >= 0)
			return -EADDRINUSE;
		g_unix[uid].abstract = 1;
		g_unix[uid].abs_len = alen;
		g_abstract_ok = 1;
		{
			unsigned i;

			for (i = 0; i < alen; i++)
				g_unix[uid].abs_name[i] = abs[i];
		}
		return 0;
	}
	if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
		return -EINVAL;
	if (!join_under_cwd(pbuf, sizeof(pbuf)))
		return -ENAMETOOLONG;
	apply_chroot(pbuf, sizeof(pbuf));
	if (ir0_early_rootfs_path_node(pbuf) != IR0_EARLY_ROOTFS_NONE ||
	    ramtab_find(pbuf) >= 0)
		return -EADDRINUSE;
	if (!parent_exists(pbuf))
		return -ENOENT;
	ret = ramtab_add(pbuf, S_IFSOCK | 0666U, 0, 0, 0);
	if (ret < 0)
		return ret;
	g_unix[uid].bound_fs = ret;
	{
		unsigned i;

		for (i = 0; pbuf[i] != '\0' && i + 1 < sizeof(g_unix[uid].bound_path);
		     i++)
			g_unix[uid].bound_path[i] = pbuf[i];
		g_unix[uid].bound_path[i] = '\0';
	}
	return 0;
}

int64_t early_rootfs_sock_listen(int fd, int backlog)
{
	int uid = unix_id_of_fd(fd);

	(void)backlog;
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (!g_unix[uid].stream)
		return -EOPNOTSUPP;
	if (g_unix[uid].bound_fs < 0 && !g_unix[uid].abstract)
		return -EINVAL;
	g_unix[uid].listening = 1;
	return 0;
}

int64_t early_rootfs_sock_connect(int fd, uint64_t addr, uint64_t addrlen)
{
	char pbuf[RAMTAB_NAME];
	int uid = unix_id_of_fd(fd);
	int dest;
	int pair;
	int idx;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (sock_pair_of_fd(fd) >= 0 && unix_id_of_fd(fd) < 0)
		return -EISCONN;
	if (uid < 0 || !g_unix[uid].used)
		return -EINVAL;
	if (g_unix[uid].listening)
		return -EINVAL;
	{
		uint8_t abs[IR0_EARLY_SUN_PATH];
		unsigned alen = 0;

		if (copy_sun_abs(addr, addrlen, abs, &alen, sizeof(abs)))
		{
			dest = unix_find_abstract(abs, alen, g_unix[uid].stream);
			if (dest < 0)
				return -ECONNREFUSED;
			if (!g_unix[uid].stream)
			{
				g_unix[uid].peer_uid = dest;
				return 0;
			}
			if (!g_unix[dest].listening)
				return -ECONNREFUSED;
		}
		else
		{
			if (!copy_sun_path(addr, addrlen, pbuf, sizeof(pbuf)))
				return -EINVAL;
			if (!join_under_cwd(pbuf, sizeof(pbuf)))
				return -ENAMETOOLONG;
			apply_chroot(pbuf, sizeof(pbuf));
			if (!g_unix[uid].stream)
			{
				int fsid = ramtab_find(pbuf);

				if (fsid < 0 ||
				    ramtab_node_kind(ramtab_table()[fsid].mode) !=
					    IR0_EARLY_ROOTFS_UNIX)
					return -ECONNREFUSED;
				g_unix[uid].peer_fs = fsid;
				return 0;
			}
			if (g_unix[uid].bound_fs >= 0 && sock_pair_of_fd(fd) >= 0)
				return -EISCONN;
			dest = unix_find_bound(pbuf, 1);
			if (dest < 0 || !g_unix[dest].listening)
				return -ECONNREFUSED;
		}
	}
	if (g_unix[dest].pending_pair >= 0)
		return -ECONNREFUSED;
	for (pair = 0; pair < SOCKPAIR_MAX; pair++)
	{
		if (!g_sock[pair].used)
			break;
	}
	if (pair >= SOCKPAIR_MAX)
		return -EMFILE;
	idx = slot_from_fd(fd);
	if (idx < 0)
		return -EBADF;
	g_sock[pair].used = 1;
	g_sock[pair].refs[0] = 1;
	g_sock[pair].refs[1] = 1;
	g_sock[pair].shut[0] = 0;
	g_sock[pair].shut[1] = 0;
	g_sock[pair].reuseaddr = 0;
	g_sock[pair].so_error = 0;
	g_sock[pair].rights_n[0] = 0;
	g_sock[pair].rights_n[1] = 0;
	g_sock[pair].passcred[0] = 0;
	g_sock[pair].passcred[1] = 0;
	g_sock[pair].cred_valid[0] = 0;
	g_sock[pair].cred_valid[1] = 0;
	g_sock[pair].cred_explicit[0] = 0;
	g_sock[pair].cred_explicit[1] = 0;
	g_sock[pair].dgram = 0;
	g_sock[pair].dir[0].rpos = 0;
	g_sock[pair].dir[0].len = 0;
	g_sock[pair].dir[1].rpos = 0;
	g_sock[pair].dir[1].len = 0;
	g_unix[dest].pending_pair = pair;
	g_unix[dest].pending_end = 1;
	g_unix[uid].refs--;
	if (g_unix[uid].refs <= 0)
		g_unix[uid].used = 0;
	g_slots[idx].pipe_id = pair << 1;
	return 0;
}

int64_t early_rootfs_sock_accept4(int fd, uint64_t addr, uint64_t addrlen,
				  int flags)
{
	int uid = unix_id_of_fd(fd);
	int newfd;
	int idx;
	int pair;
	int end;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (flags & ~(IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	if (uid < 0 || !g_unix[uid].used || !g_unix[uid].listening)
		return -EINVAL;
	if (!g_unix[uid].stream)
		return -EOPNOTSUPP;
	if (g_unix[uid].pending_pair < 0)
		return wait_or_eagain(fd);
	pair = g_unix[uid].pending_pair;
	end = g_unix[uid].pending_end;
	g_unix[uid].pending_pair = -1;
	newfd = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (newfd < 0)
	{
		g_unix[uid].pending_pair = pair;
		return newfd;
	}
	idx = slot_from_fd(newfd);
	if (idx < 0)
	{
		close_fd(newfd);
		g_unix[uid].pending_pair = pair;
		return -EBADF;
	}
	g_slots[idx].pipe_id = (pair << 1) | end;
	set_flags_of_fd(newfd, IR0_O_RDWR |
				((flags & IR0_EARLY_SOCK_NONBLOCK) ?
					 LINUX_O_NONBLOCK :
					 0));
	set_cloexec_of_fd(newfd, (flags & IR0_EARLY_SOCK_CLOEXEC) != 0);
	if (write_sun_name(addr, addrlen, 0) < 0)
	{
		close_fd(newfd);
		return -EFAULT;
	}
	return newfd;
}

int64_t early_rootfs_getsockname(int fd, uint64_t addr, uint64_t addrlen)
{
	int uid = unix_id_of_fd(fd);

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0 && g_unix[uid].used && g_unix[uid].abstract)
		return write_sun_abs(addr, addrlen, g_unix[uid].abs_name,
				     g_unix[uid].abs_len);
	if (uid >= 0 && g_unix[uid].used && g_unix[uid].bound_path[0])
		return write_sun_name(addr, addrlen, g_unix[uid].bound_path);
	return write_sun_name(addr, addrlen, 0);
}

int64_t early_rootfs_getpeername(int fd, uint64_t addr, uint64_t addrlen)
{
	int uid = unix_id_of_fd(fd);

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream || g_unix[uid].peer_fs < 0)
			return -ENOTCONN;
		if (g_unix[uid].peer_fs >= RAMTAB_MAX ||
		    !ramtab_table()[g_unix[uid].peer_fs].used)
			return -ENOTCONN;
		return write_sun_name(addr, addrlen,
				      ramtab_table()[g_unix[uid].peer_fs].path);
	}
	if (sock_pair_of_fd(fd) < 0)
		return -ENOTCONN;
	return write_sun_name(addr, addrlen, 0);
}

int64_t early_rootfs_sendto(int fd, uint64_t buffer, uint64_t count,
			    uint64_t dest, uint64_t destlen)
{
	int uid = unix_id_of_fd(fd);
	char pbuf[RAMTAB_NAME];
	uint8_t tmp[256];
	int dest_uid;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (uid >= 0 && !g_unix[uid].stream && dest && destlen)
	{
		uint8_t abs[IR0_EARLY_SUN_PATH];
		unsigned alen = 0;

		if (copy_sun_abs(dest, destlen, abs, &alen, sizeof(abs)))
		{
			dest_uid = unix_find_abstract(abs, alen, 0);
			if (dest_uid < 0)
				return -ECONNREFUSED;
			if (count == 0)
				return 0;
			if (count > 250)
				return -EMSGSIZE;
			if (!g_user || !g_user->copy_from_user ||
			    g_user->copy_from_user(tmp, buffer, count) != 0)
				return -EFAULT;
			{
				int64_t sent = unix_dgram_deposit(dest_uid, tmp,
								  (unsigned)count);

				if (sent >= 0)
					g_abstract_ok = 1;
				return sent;
			}
		}
		if (!copy_sun_path(dest, destlen, pbuf, sizeof(pbuf)))
			return -EINVAL;
		if (!join_under_cwd(pbuf, sizeof(pbuf)))
			return -ENAMETOOLONG;
		apply_chroot(pbuf, sizeof(pbuf));
		dest_uid = unix_find_bound(pbuf, 0);
		if (dest_uid < 0)
			return -ECONNREFUSED;
		if (count == 0)
			return 0;
		if (count > 250)
			return -EMSGSIZE;
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, count) != 0)
			return -EFAULT;
		return unix_dgram_deposit(dest_uid, tmp, (unsigned)count);
	}
	if (uid >= 0 && g_unix[uid].stream && dest && destlen)
		return -EISCONN;
	return early_rootfs_sock_write(fd, buffer, count);
}

int64_t early_rootfs_recvfrom(int fd, uint64_t buffer, uint64_t count)
{
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	return early_rootfs_read(fd, buffer, count);
}

int64_t early_rootfs_shutdown(int fd, int how)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int end = pipe_id_of_fd(fd) & 1;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (how != IR0_EARLY_SHUT_RD && how != IR0_EARLY_SHUT_WR &&
	    how != IR0_EARLY_SHUT_RDWR)
		return -EINVAL;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream && g_unix[uid].listening)
			return -ENOTCONN;
		if (g_unix[uid].stream)
			return -ENOTCONN;
		if (how != IR0_EARLY_SHUT_WR && how != IR0_EARLY_SHUT_RDWR)
			g_unix[uid].shut_rd = 1;
		if (how != IR0_EARLY_SHUT_RD)
			g_unix[uid].shut_wr = 1;
		return 0;
	}
	if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
		return -ENOTCONN;
	if (how != IR0_EARLY_SHUT_WR)
		g_sock[pair].shut[end] |= 1;
	if (how != IR0_EARLY_SHUT_RD)
		g_sock[pair].shut[end] |= 2;
	return 0;
}

int64_t early_rootfs_setsockopt(int fd, int level, int optname, uint64_t optval,
				uint64_t optlen)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int value = 0;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (level != IR0_EARLY_SOL_SOCKET)
		return -ENOPROTOOPT;
	if (optname != IR0_EARLY_SO_REUSEADDR &&
	    optname != IR0_EARLY_SO_PASSCRED)
		return -ENOPROTOOPT;
	if (optlen < 4 || !optval)
		return -EINVAL;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(&value, optval, 4) != 0)
		return -EFAULT;
	if (uid >= 0 && g_unix[uid].used)
	{
		if (optname == IR0_EARLY_SO_PASSCRED)
			g_unix[uid].passcred = value ? 1 : 0;
		else
			g_unix[uid].reuseaddr = value ? 1 : 0;
	}
	else if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		int end = pipe_id_of_fd(fd) & 1;

		if (optname == IR0_EARLY_SO_PASSCRED)
			g_sock[pair].passcred[end] = value ? 1 : 0;
		else
			g_sock[pair].reuseaddr = value ? 1 : 0;
	}
	else
		return -EBADF;
	return 0;
}

int64_t early_rootfs_getsockopt(int fd, int level, int optname, uint64_t optval,
				uint64_t optlen)
{
	int uid = unix_id_of_fd(fd);
	int pair = sock_pair_of_fd(fd);
	int value = 0;
	uint32_t len = 4;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -ENOTSOCK;
	if (level != IR0_EARLY_SOL_SOCKET)
		return -ENOPROTOOPT;
	if (!optlen || !g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(&len, optlen, 4) != 0)
		return -EFAULT;
	if (len < 4 || !optval)
		return -EINVAL;
	if (uid >= 0 && g_unix[uid].used)
	{
		if (optname == IR0_EARLY_SO_TYPE)
			value = g_unix[uid].stream ? IR0_EARLY_SOCK_STREAM :
						     IR0_EARLY_SOCK_DGRAM;
		else if (optname == IR0_EARLY_SO_ERROR)
		{
			value = g_unix[uid].so_error;
			g_unix[uid].so_error = 0;
		}
		else if (optname == IR0_EARLY_SO_REUSEADDR)
			value = g_unix[uid].reuseaddr;
		else if (optname == IR0_EARLY_SO_PASSCRED)
			value = g_unix[uid].passcred;
		else
			return -ENOPROTOOPT;
	}
	else if (pair >= 0 && pair < SOCKPAIR_MAX && g_sock[pair].used)
	{
		if (optname == IR0_EARLY_SO_TYPE)
			value = g_sock[pair].dgram ? IR0_EARLY_SOCK_DGRAM :
						     IR0_EARLY_SOCK_STREAM;
		else if (optname == IR0_EARLY_SO_ERROR)
		{
			value = g_sock[pair].so_error;
			g_sock[pair].so_error = 0;
		}
		else if (optname == IR0_EARLY_SO_REUSEADDR)
			value = g_sock[pair].reuseaddr;
		else if (optname == IR0_EARLY_SO_PASSCRED)
			value = g_sock[pair].passcred[pipe_id_of_fd(fd) & 1];
		else
			return -ENOPROTOOPT;
	}
	else
		return -EBADF;
	len = 4;
	if (g_user->copy_to_user(optval, &value, 4) != 0 ||
	    g_user->copy_to_user(optlen, &len, 4) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_fifo_write(int fd, uint64_t buffer, uint64_t count)
{
	int fsid = ramtab_body(pipe_id_of_fd(fd));
	int pipe_id;
	struct early_pipe *p;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FIFO)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
		return -EBADF;
	pipe_id = ramtab_table()[fsid].fifo_pipe;
	if (pipe_id < 0 || pipe_id >= PIPE_MAX)
		return -EBADF;
	p = &g_pipes[pipe_id];
	if (!p->used)
		return -EBADF;
	if (p->readers == 0)
		return -EPIPE;
	if (count == 0)
		return 0;
	room = PIPE_BUF - p->len;
	if (room == 0)
		return wait_or_eagain(fd);
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	put = pipe_deposit(p, tmp, put);
	return (int64_t)put;
}

int64_t early_rootfs_socketpair(int domain, int type, int protocol, uint64_t sv)
{
	int pair;
	int a;
	int b;
	int aidx;
	int bidx;
	int fds[2];
	int kind = type & 0xf;

	if (!g_ready)
		return -ENOSYS;
	if (domain != IR0_EARLY_AF_UNIX)
		return -EAFNOSUPPORT;
	if (protocol != 0)
		return -EPROTONOSUPPORT;
	if (kind != IR0_EARLY_SOCK_STREAM && kind != IR0_EARLY_SOCK_DGRAM)
		return -ESOCKTNOSUPPORT;
	if (type & ~(0xf | IR0_EARLY_SOCK_CLOEXEC | IR0_EARLY_SOCK_NONBLOCK))
		return -EINVAL;
	if (!sv || !g_user || !g_user->copy_to_user)
		return -EFAULT;
	for (pair = 0; pair < SOCKPAIR_MAX; pair++)
	{
		if (!g_sock[pair].used)
			break;
	}
	if (pair >= SOCKPAIR_MAX)
		return -EMFILE;
	a = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (a < 0)
		return a;
	b = alloc_fd(IR0_EARLY_ROOTFS_SOCK);
	if (b < 0)
	{
		close_fd(a);
		return b;
	}
	aidx = slot_from_fd(a);
	bidx = slot_from_fd(b);
	if (aidx < 0 || bidx < 0)
	{
		close_fd(a);
		close_fd(b);
		return -EBADF;
	}
	g_sock[pair].used = 1;
	g_sock[pair].refs[0] = 1;
	g_sock[pair].refs[1] = 1;
	g_sock[pair].shut[0] = 0;
	g_sock[pair].shut[1] = 0;
	g_sock[pair].reuseaddr = 0;
	g_sock[pair].so_error = 0;
	g_sock[pair].rights_n[0] = 0;
	g_sock[pair].rights_n[1] = 0;
	g_sock[pair].passcred[0] = 0;
	g_sock[pair].passcred[1] = 0;
	g_sock[pair].cred_valid[0] = 0;
	g_sock[pair].cred_valid[1] = 0;
	g_sock[pair].cred_explicit[0] = 0;
	g_sock[pair].cred_explicit[1] = 0;
	g_sock[pair].dgram = (kind == IR0_EARLY_SOCK_DGRAM);
	g_sock[pair].dir[0].rpos = 0;
	g_sock[pair].dir[0].len = 0;
	g_sock[pair].dir[1].rpos = 0;
	g_sock[pair].dir[1].len = 0;
	g_slots[aidx].pipe_id = pair << 1;
	g_slots[bidx].pipe_id = (pair << 1) | 1;
	set_flags_of_fd(a, IR0_O_RDWR |
			   ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							      0));
	set_flags_of_fd(b, IR0_O_RDWR |
			   ((type & IR0_EARLY_SOCK_NONBLOCK) ? LINUX_O_NONBLOCK :
							      0));
	set_cloexec_of_fd(a, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	set_cloexec_of_fd(b, (type & IR0_EARLY_SOCK_CLOEXEC) != 0);
	fds[0] = a;
	fds[1] = b;
	if (g_user->copy_to_user(sv, fds, sizeof(fds)) != 0)
	{
		close_fd(a);
		close_fd(b);
		return -EFAULT;
	}
	return 0;
}

static int64_t unix_dgram_deposit(int uid, const uint8_t *tmp, unsigned count)
{
	unsigned room;
	uint8_t hdr[2];

	if (uid < 0 || uid >= UNIX_MAX || !g_unix[uid].used || g_unix[uid].stream)
		return -ECONNREFUSED;
	if (count > 250)
		return -EMSGSIZE;
	room = PIPE_BUF - g_unix[uid].len;
	if (room < count + 2U)
		return -EAGAIN;
	hdr[0] = (uint8_t)(count & 0xffU);
	hdr[1] = (uint8_t)((count >> 8) & 0xffU);
	if (early_unix_deposit(&g_unix[uid], hdr, 2) != 2 ||
	    early_unix_deposit(&g_unix[uid], tmp, count) != count)
		return -EAGAIN;
	note_sender_creds_unix(uid);
	return (int64_t)count;
}

int unix_find_bound(const char *path, int stream)
{
	int fsid;
	int i;

	fsid = ramtab_find(path);
	if (fsid < 0 || ramtab_node_kind(ramtab_table()[fsid].mode) != IR0_EARLY_ROOTFS_UNIX)
		return -1;
	fsid = ramtab_body(fsid);
	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && ramtab_body(g_unix[i].bound_fs) == fsid &&
		    g_unix[i].stream == stream)
			return i;
	}
	return -1;
}

int64_t early_rootfs_sock_write(int fd, uint64_t buffer, uint64_t count)
{
	int encoded = pipe_id_of_fd(fd);
	int uid = unix_id_of_fd(fd);
	int pair = encoded >> 1;
	int end = encoded & 1;
	int dst = end;
	uint8_t tmp[256];
	unsigned room;
	unsigned put;

	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_SOCK)
		return -EBADF;
	if (uid >= 0)
	{
		if (!g_unix[uid].used)
			return -EBADF;
		if (g_unix[uid].stream)
			return -ENOTCONN;
		if (g_unix[uid].shut_wr)
			return -EPIPE;
		{
			int dest = g_unix[uid].peer_uid;

			if (dest < 0)
			{
				if (g_unix[uid].peer_fs < 0 ||
				    g_unix[uid].peer_fs >= RAMTAB_MAX ||
				    !ramtab_table()[g_unix[uid].peer_fs].used)
					return -ENOTCONN;
				dest = unix_find_bound(ramtab_table()[g_unix[uid].peer_fs].path,
						       0);
			}
			if (dest < 0)
				return -ECONNREFUSED;
			if (count == 0)
				return 0;
			if (count > 250)
				return -EMSGSIZE;
			if (!g_user || !g_user->copy_from_user ||
			    g_user->copy_from_user(tmp, buffer, count) != 0)
				return -EFAULT;
			return unix_dgram_deposit(dest, tmp, (unsigned)count);
		}
	}
	if (pair < 0 || pair >= SOCKPAIR_MAX || !g_sock[pair].used)
		return -EBADF;
	if (g_sock[pair].shut[end] & 2)
		return -EPIPE;
	if (g_sock[pair].refs[1 - end] == 0 || (g_sock[pair].shut[1 - end] & 1))
		return -EPIPE;
	if (count == 0)
		return 0;
	room = PIPE_BUF - g_sock[pair].dir[dst].len;
	if (g_sock[pair].dgram)
	{
		if (count > 250)
			return -EMSGSIZE;
		if (room < count + 2)
			return wait_or_eagain(fd);
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, count) != 0)
			return -EFAULT;
		{
			uint8_t hdr[2];

			hdr[0] = (uint8_t)(count & 0xffU);
			hdr[1] = (uint8_t)((count >> 8) & 0xffU);
			if (early_sock_deposit(&g_sock[pair], dst, hdr, 2) != 2 ||
			    early_sock_deposit(&g_sock[pair], dst, tmp,
					       (unsigned)count) != (unsigned)count)
				return wait_or_eagain(fd);
		}
		note_sender_creds_sock(pair, dst);
		return (int64_t)count;
	}
	if (room == 0)
		return wait_or_eagain(fd);
	put = room;
	if (put > sizeof(tmp))
		put = (unsigned)sizeof(tmp);
	if ((uint64_t)put > count)
		put = (unsigned)count;
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, put) != 0)
		return -EFAULT;
	put = early_sock_deposit(&g_sock[pair], dst, tmp, put);
	note_sender_creds_sock(pair, dst);
	return (int64_t)put;
}
