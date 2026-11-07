/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_file.c
 * Description: early file I/O (pread/pwrite/splice/sendfile/fallocate).
 */

#include "erfs_internal.h"

int64_t early_rootfs_file_write(int fd, uint64_t buffer, uint64_t count)
{
	int fsid = pipe_id_of_fd(fd);
	uint64_t *pos = pos_of_fd(fd);
	uint8_t tmp[256];
	uint64_t n;
	uint64_t i;
	uint64_t start;

	if (vf_of_fd(fd))
		return product_xfer(fd, buffer, count, 1);
	if (node_of_fd(fd) != IR0_EARLY_ROOTFS_FILE)
		return -EBADF;
	if ((flags_of_fd(fd) & LINUX_O_ACCMODE) == 0)
		return -EBADF;
	fsid = ramtab_body(fsid);
	if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used || !pos)
		return -EBADF;
	if (!ramtab_table()[fsid].writable)
		return -EROFS;
	if (count == 0)
		return 0;
	start = *pos;
	if (start >= RAMTAB_FILE_CAP)
		return -EFBIG;
	n = count;
	if (start + n > RAMTAB_FILE_CAP)
		n = RAMTAB_FILE_CAP - start;
	if (n > sizeof(tmp))
		n = sizeof(tmp);
	if (!g_user || !g_user->copy_from_user ||
	    g_user->copy_from_user(tmp, buffer, n) != 0)
		return -EFAULT;
	for (i = 0; i < n; i++)
		ramtab_table()[fsid].own[start + i] = tmp[i];
	*pos = start + n;
	if (*pos > ramtab_table()[fsid].size)
		ramtab_table()[fsid].size = (uint32_t)*pos;
	ramtab_table()[fsid].data = ramtab_table()[fsid].own;
	return (int64_t)n;
}

int64_t early_rootfs_blkgetsize64(int fd, uint64_t dest)
{
	uint64_t bytes;

	if (!early_rootfs_fd_is_block(fd) || !vda_present())
		return -ENOTTY;
	if (!dest || !g_user || !g_user->copy_to_user)
		return -EFAULT;
	bytes = vda_bytes();
	return g_user->copy_to_user(dest, &bytes, sizeof(bytes)) == 0 ?
	       0 : -EFAULT;
}

static int64_t kread_at(int fd, uint64_t off, uint8_t *tmp, uint64_t count)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	uint64_t n;
	uint64_t i;
	uint64_t limit;

	if (!tmp || !count)
		return 0;
	if (node == IR0_EARLY_ROOTFS_NULL)
		return 0;
	if (node == IR0_EARLY_ROOTFS_ZERO)
	{
		n = count > 256 ? 256 : count;
		for (i = 0; i < n; i++)
			tmp[i] = 0;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_INIT)
	{
		if (off >= g_init_vis || !g_blob)
			return 0;
		limit = g_init_vis - off;
		if (off >= g_blob_len)
			return 0;
		if (limit > g_blob_len - off)
			limit = g_blob_len - off;
		n = count > limit ? limit : count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = g_blob[off + i];
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used)
			return -EBADF;
		if (off >= ramtab_table()[fsid].size)
			return 0;
		n = ramtab_table()[fsid].size - off;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = ramtab_table()[fsid].data ? ramtab_table()[fsid].data[off + i] : 0;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used)
			return -EBADF;
		if (off >= g_memfd[mid].len)
			return 0;
		n = g_memfd[mid].len - off;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = g_memfd[mid].data[off + i];
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_VDA)
	{
		uint8_t sector[512];
		uint64_t lba;
		uint64_t skip;

		if (!vda_present())
			return -EIO;
		if (off >= vda_bytes())
			return 0;
		lba = off / 512ULL;
		skip = off % 512ULL;
		if (ir0_block_read_by_name("vda", lba, 1, sector) != 0)
			return -EIO;
		n = 512ULL - skip;
		if (n > count)
			n = count;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			tmp[i] = sector[skip + i];
		return (int64_t)n;
	}
	return -ESPIPE;
}

static int64_t kwrite_buf(int fd, const uint8_t *tmp, uint64_t count)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	uint64_t *pos;
	uint64_t n;
	uint64_t i;
	uint64_t start;

	if (!tmp)
		return -EFAULT;
	if (count == 0)
		return 0;
	if (node == IR0_EARLY_ROOTFS_NULL || node == IR0_EARLY_ROOTFS_ZERO)
		return (int64_t)(count > 256 ? 256 : count);
	if (node == IR0_EARLY_ROOTFS_PIPE_W)
	{
		int pipe_id = pipe_id_of_fd(fd);
		struct early_pipe *p;
		unsigned room;
		unsigned put;

		if (pipe_id < 0)
			return -EBADF;
		p = &g_pipes[pipe_id];
		if (!p->used)
			return -EBADF;
		if (p->readers == 0)
			return -EPIPE;
		room = PIPE_BUF - p->len;
		if (room == 0)
			return wait_or_eagain(fd);
		put = room;
		if (put > 256)
			put = 256;
		if ((uint64_t)put > count)
			put = (unsigned)count;
		put = pipe_deposit(p, tmp, put);
		return (int64_t)put;
	}
	if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		pos = pos_of_fd(fd);
		if (fsid < 0 || fsid >= RAMTAB_MAX || !ramtab_table()[fsid].used || !pos)
			return -EBADF;
		if (!ramtab_table()[fsid].writable)
			return -EROFS;
		start = *pos;
		if (start >= RAMTAB_FILE_CAP)
			return -EFBIG;
		n = count;
		if (start + n > RAMTAB_FILE_CAP)
			n = RAMTAB_FILE_CAP - start;
		if (n > 256)
			n = 256;
		for (i = 0; i < n; i++)
			ramtab_table()[fsid].own[start + i] = tmp[i];
		*pos = start + n;
		if (*pos > ramtab_table()[fsid].size)
			ramtab_table()[fsid].size = (uint32_t)*pos;
		ramtab_table()[fsid].data = ramtab_table()[fsid].own;
		return (int64_t)n;
	}
	if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		pos = pos_of_fd(fd);
		if (mid < 0 || mid >= MEMFD_MAX || !g_memfd[mid].used || !pos)
			return -EBADF;
		start = *pos;
		if (start >= MEMFD_CAP)
			return -EINVAL;
		n = count;
		if (start + n > MEMFD_CAP)
			n = MEMFD_CAP - start;
		if (n > 256)
			n = 256;
		if (n == 0)
			return -EINVAL;
		for (i = 0; i < n; i++)
			g_memfd[mid].data[start + i] = tmp[i];
		if (start + n > g_memfd[mid].len)
			g_memfd[mid].len = (uint32_t)(start + n);
		*pos = start + n;
		return (int64_t)n;
	}
	return -EBADF;
}

int64_t early_rootfs_pread64(int fd, uint64_t buffer, uint64_t count,
			     int64_t offset)
{
	uint8_t tmp[256];
	int64_t n;
	int err;

	if (offset < 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	n = kread_at(fd, (uint64_t)offset, tmp, count);
	if (n <= 0)
		return n;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, tmp, (uint64_t)n) != 0)
		return -EFAULT;
	return n;
}

int64_t early_rootfs_pwrite64(int fd, uint64_t buffer, uint64_t count,
			      int64_t offset)
{
	uint64_t *pos;
	uint64_t saved;
	int64_t n;
	int err;

	if (offset < 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	pos = pos_of_fd(fd);
	if (!pos)
		return -EBADF;
	saved = *pos;
	*pos = (uint64_t)offset;
	if (node_of_fd(fd) == IR0_EARLY_ROOTFS_MEMFD)
		n = early_rootfs_memfd_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_FILE)
		n = early_rootfs_file_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_VDA)
		n = early_rootfs_block_write(fd, buffer, count);
	else if (node_of_fd(fd) == IR0_EARLY_ROOTFS_NULL ||
		 node_of_fd(fd) == IR0_EARLY_ROOTFS_ZERO)
	{
		uint8_t discard[256];

		n = (int64_t)(count > sizeof(discard) ? sizeof(discard) : count);
		if (n && (!g_user || !g_user->copy_from_user ||
			  g_user->copy_from_user(discard, buffer, (uint64_t)n) !=
				  0))
			n = -EFAULT;
	}
	else
		n = -EINVAL;
	*pos = saved;
	return n;
}

int64_t early_rootfs_sendfile(int out_fd, int in_fd, uint64_t offset,
			      uint64_t count)
{
	uint8_t tmp[256];
	uint64_t *inpos;
	int64_t off = 0;
	uint64_t done = 0;
	int err;

	err = fd_espipe(in_fd);
	if (err < 0)
		return err;
	if (node_of_fd(out_fd) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (offset)
	{
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(&off, offset, sizeof(off)) != 0)
			return -EFAULT;
		if (off < 0)
			return -EINVAL;
	}
	else
	{
		inpos = pos_of_fd(in_fd);
		if (!inpos)
			return -EBADF;
		off = (int64_t)*inpos;
	}
	while (done < count)
	{
		int64_t n;
		int64_t w;

		n = kread_at(in_fd, (uint64_t)off, tmp, count - done);
		if (n < 0)
			return done ? (int64_t)done : n;
		if (n == 0)
			break;
		w = kwrite_buf(out_fd, tmp, (uint64_t)n);
		if (w < 0)
			return done ? (int64_t)done : w;
		off += w;
		done += (uint64_t)w;
		if (w < n)
			break;
	}
	if (offset)
	{
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(offset, &off, sizeof(off)) != 0)
			return -EFAULT;
	}
	else
	{
		inpos = pos_of_fd(in_fd);
		if (inpos)
			*inpos = (uint64_t)off;
	}
	return (int64_t)done;
}

static int64_t kwrite_at(int fd, uint64_t off, const uint8_t *tmp, uint64_t n)
{
	uint64_t *pos = pos_of_fd(fd);
	uint64_t saved;
	int64_t w;

	if (!pos)
		return -EBADF;
	saved = *pos;
	*pos = off;
	w = kwrite_buf(fd, tmp, n);
	*pos = saved;
	return w;
}

int64_t early_rootfs_splice(int fd_in, uint64_t off_in, int fd_out,
			    uint64_t off_out, uint64_t len, unsigned flags)
{
	struct early_pipe *pin;
	struct early_pipe *pout;
	uint8_t tmp[256];
	uint64_t *inpos;
	uint64_t *outpos;
	int64_t in_off = 0;
	int64_t out_off = 0;
	unsigned n;
	int64_t got;
	int64_t put;
	int err;

	if (len == 0)
		return 0;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	if (node_of_fd(fd_in) == IR0_EARLY_ROOTFS_NONE ||
	    node_of_fd(fd_out) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	pin = pipe_of_fd(fd_in);
	pout = pipe_of_fd(fd_out);
	if (!pin && !pout)
		return -EINVAL;
	if (pin && off_in)
		return -ESPIPE;
	if (pout && off_out)
		return -ESPIPE;
	if (pin && pout && pin == pout)
		return -EINVAL;
	if (!pin)
	{
		err = fd_espipe(fd_in);
		if (err < 0)
			return err;
		inpos = pos_of_fd(fd_in);
		err = load_loff(off_in, &in_off, inpos ? (int64_t)*inpos : 0);
		if (err < 0)
			return err;
	}
	if (!pout)
	{
		err = fd_espipe(fd_out);
		if (err < 0)
			return err;
		outpos = pos_of_fd(fd_out);
		err = load_loff(off_out, &out_off, outpos ? (int64_t)*outpos : 0);
		if (err < 0)
			return err;
	}
	n = len > 256 ? 256 : (unsigned)len;
	if (pin && pout)
	{
		if (pin->len == 0)
			return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
		if (pout->readers == 0)
			return -EPIPE;
		if (pout->len == PIPE_BUF)
			return splice_wait(fd_out, flags);
		n = pipe_consume(pin, tmp, n);
		n = pipe_deposit(pout, tmp, n);
		return (int64_t)n;
	}
	if (pin)
	{
		if (pin->len == 0)
			return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
		n = pipe_consume(pin, tmp, n);
		put = kwrite_at(fd_out, (uint64_t)out_off, tmp, n);
		if (put < 0)
			return put;
		out_off += put;
		if (off_out)
		{
			err = store_loff(off_out, out_off);
			if (err < 0)
				return err;
		}
		else
		{
			outpos = pos_of_fd(fd_out);
			if (outpos)
				*outpos = (uint64_t)out_off;
		}
		return put;
	}
	if (pout->readers == 0)
		return -EPIPE;
	if (pout->len == PIPE_BUF)
		return splice_wait(fd_out, flags);
	got = kread_at(fd_in, (uint64_t)in_off, tmp, n);
	if (got <= 0)
		return got;
	n = pipe_deposit(pout, tmp, (unsigned)got);
	in_off += n;
	if (off_in)
	{
		err = store_loff(off_in, in_off);
		if (err < 0)
			return err;
	}
	else
	{
		inpos = pos_of_fd(fd_in);
		if (inpos)
			*inpos = (uint64_t)in_off;
	}
	return (int64_t)n;
}

int64_t early_rootfs_tee(int fd_in, int fd_out, uint64_t len, unsigned flags)
{
	struct early_pipe *pin;
	struct early_pipe *pout;
	uint8_t tmp[256];
	unsigned n;

	if (len == 0)
		return 0;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	pin = pipe_of_fd(fd_in);
	pout = pipe_of_fd(fd_out);
	if (!pin || !pout)
		return -EINVAL;
	if (pin == pout)
		return -EINVAL;
	if (pin->len == 0)
		return pin->writers == 0 ? 0 : splice_wait(fd_in, flags);
	if (pout->readers == 0)
		return -EPIPE;
	if (pout->len == PIPE_BUF)
		return splice_wait(fd_out, flags);
	n = len > 256 ? 256 : (unsigned)len;
	n = pipe_peek(pin, tmp, n);
	return (int64_t)pipe_deposit(pout, tmp, n);
}

int64_t early_rootfs_copy_file_range(int fd_in, uint64_t off_in, int fd_out,
				     uint64_t off_out, uint64_t len,
				     unsigned flags)
{
	uint8_t tmp[256];
	uint64_t *inpos;
	uint64_t *outpos;
	int64_t in_off;
	int64_t out_off;
	int64_t got;
	int64_t put;
	int err;

	if (flags)
		return -EINVAL;
	if (len == 0)
		return 0;
	err = fd_espipe(fd_in);
	if (err < 0)
		return err;
	err = fd_espipe(fd_out);
	if (err < 0)
		return err;
	inpos = pos_of_fd(fd_in);
	outpos = pos_of_fd(fd_out);
	err = load_loff(off_in, &in_off, inpos ? (int64_t)*inpos : 0);
	if (err < 0)
		return err;
	err = load_loff(off_out, &out_off, outpos ? (int64_t)*outpos : 0);
	if (err < 0)
		return err;
	if (fd_in == fd_out ||
	    (node_of_fd(fd_in) == node_of_fd(fd_out) &&
	     pipe_id_of_fd(fd_in) == pipe_id_of_fd(fd_out)))
	{
		int64_t in_end = in_off + (int64_t)len;
		int64_t out_end = out_off + (int64_t)len;

		if (in_off < out_end && out_off < in_end)
			return -EINVAL;
	}
	got = kread_at(fd_in, (uint64_t)in_off, tmp, len > 256 ? 256 : len);
	if (got <= 0)
		return got;
	put = kwrite_at(fd_out, (uint64_t)out_off, tmp, (uint64_t)got);
	if (put < 0)
		return put;
	in_off += put;
	out_off += put;
	if (off_in)
	{
		err = store_loff(off_in, in_off);
		if (err < 0)
			return err;
	}
	else if (inpos)
		*inpos = (uint64_t)in_off;
	if (off_out)
	{
		err = store_loff(off_out, out_off);
		if (err < 0)
			return err;
	}
	else if (outpos)
		*outpos = (uint64_t)out_off;
	return put;
}

int64_t early_rootfs_fadvise64(int fd, int64_t offset, uint64_t len, int advice)
{
	int err;

	(void)len;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	if (offset < 0 || !ir0_early_fadvise_ok(advice))
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_vmsplice(int fd, uint64_t iov, uint64_t iovcnt,
			      unsigned flags)
{
	struct early_pipe *p = pipe_of_fd(fd);
	enum ir0_early_rootfs_node node = node_of_fd(fd);
	int acc = flags_of_fd(fd) & LINUX_O_ACCMODE;
	uint8_t tmp[256];
	uint64_t i;
	int64_t total = 0;

	if (!p)
		return node == IR0_EARLY_ROOTFS_NONE ? -EBADF : -EINVAL;
	if (!ir0_early_splice_flags_ok(flags))
		return -EINVAL;
	if (iovcnt > 8)
		return -EINVAL;
	for (i = 0; i < iovcnt; i++)
	{
		uint64_t vec[2];
		unsigned n;

		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(vec, iov + i * 16UL, sizeof(vec)) != 0)
			return total ? total : -EFAULT;
		if (vec[1] == 0)
			continue;
		n = vec[1] > 256 ? 256 : (unsigned)vec[1];
		if (node == IR0_EARLY_ROOTFS_PIPE_W ||
		    (node == IR0_EARLY_ROOTFS_FIFO && acc != 0))
		{
			if (p->readers == 0)
				return total ? total : -EPIPE;
			if (p->len == PIPE_BUF)
				return total ? total : splice_wait(fd, flags);
			if (g_user->copy_from_user(tmp, vec[0], n) != 0)
				return total ? total : -EFAULT;
			n = pipe_deposit(p, tmp, n);
		}
		else
		{
			if (p->len == 0)
				return total ? total :
					       (p->writers == 0 ?
							0 :
							splice_wait(fd, flags));
			n = pipe_consume(p, tmp, n);
			if (!g_user->copy_to_user ||
			    g_user->copy_to_user(vec[0], tmp, n) != 0)
				return total ? total : -EFAULT;
		}
		total += n;
		if ((uint64_t)n < vec[1])
			break;
	}
	return total;
}

int64_t early_rootfs_sync_file_range(int fd, int64_t offset, int64_t nbytes,
				     unsigned flags)
{
	int err;

	if (offset < 0 || nbytes < 0)
		return -EINVAL;
	if (!ir0_early_sync_file_range_flags_ok(flags))
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	return 0;
}

int64_t early_rootfs_readahead(int fd, int64_t offset, uint64_t count)
{
	int err;

	(void)count;
	err = fd_espipe(fd);
	if (err < 0)
		return err == -ESPIPE ? -EINVAL : err;
	if (offset < 0)
		return -EINVAL;
	return 0;
}

int64_t early_rootfs_syncfs(int fd)
{
	if (node_of_fd(fd) == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	return 0;
}

int64_t early_rootfs_fallocate(int fd, int mode, int64_t offset, int64_t len)
{
	uint64_t end;
	int err;

	if (offset < 0 || len <= 0)
		return -EINVAL;
	err = fd_espipe(fd);
	if (err < 0)
		return err;
	if (mode & ~IR0_EARLY_FALLOC_KEEP_SIZE)
		return -EOPNOTSUPP;
	end = (uint64_t)offset + (uint64_t)len;
	if (end < (uint64_t)offset)
		return -EFBIG;
	if (mode & IR0_EARLY_FALLOC_KEEP_SIZE)
		return 0;
	return early_rootfs_ftruncate(fd, end);
}
