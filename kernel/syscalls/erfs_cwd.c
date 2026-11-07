/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_cwd.c
 * Description: Early rootfs cwd binding, chdir, directory iteration, getcwd, lseek.
 */

#include "erfs_internal.h"

static const char *early_cwd_string(void)
{
	if (g_cwd == IR0_EARLY_ROOTFS_DEV)
		return (g_chroot == IR0_EARLY_ROOTFS_DEV) ? "/" : "/dev";
	if (g_cwd == IR0_EARLY_ROOTFS_TMP)
		return (g_chroot == IR0_EARLY_ROOTFS_TMP) ? "/" : "/tmp";
	if (g_cwd == IR0_EARLY_ROOTFS_USERDIR)
		return g_cwd_path[0] ? g_cwd_path : "/";
	return "/";
}

static const char *early_root_string(void)
{
	if (g_chroot == IR0_EARLY_ROOTFS_DEV)
		return "/dev";
	if (g_chroot == IR0_EARLY_ROOTFS_TMP)
		return "/tmp";
	if (g_chroot_path[0])
		return g_chroot_path;
	return "/";
}

static void early_publish_cwd(void)
{
	if (g_cwd_ops && g_cwd_ops->set)
		g_cwd_ops->set(early_cwd_string());
}

void early_publish_root(void)
{
	if (g_cwd_ops && g_cwd_ops->set_root)
		g_cwd_ops->set_root(early_root_string());
}

void early_rootfs_bind_cwd(const struct ir0_cwd_ops *ops)
{
	g_cwd_ops = ops;
	g_cwd_used = 0;
	early_publish_cwd();
	early_publish_root();
}

int early_rootfs_cwd_used(void)
{
	return g_cwd_used;
}

int64_t early_rootfs_chdir(uint64_t path)
{
	char pbuf[32];
	enum ir0_early_rootfs_node node;

	if (!g_ready)
		return -ENOENT;
	if (!copy_user_cstr(path, pbuf, sizeof(pbuf)))
		return -EFAULT;
	node = lookup_user_path(pbuf);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -ENOENT;
	if (!node_is_dir(node))
		return -ENOTDIR;
	g_cwd = node;
	{
		unsigned i;

		for (i = 0; pbuf[i] != '\0' && i + 1 < sizeof(g_cwd_path); i++)
			g_cwd_path[i] = pbuf[i];
		g_cwd_path[i] = '\0';
	}
	early_publish_cwd();
	return 0;
}

int64_t early_rootfs_getdents64(int fd, uint64_t buffer, uint64_t count)
{
	enum ir0_early_rootfs_node node;
	int idx;
	unsigned total;
	unsigned written = 0;
	uint8_t rec[32];
	uint8_t tmp[256];
	unsigned tmp_used = 0;

	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (!node_is_dir(node))
		return -ENOTDIR;
	{
		char dirpath[RAMTAB_NAME];
		int denied;

		if (path_of_fd(fd, dirpath, sizeof(dirpath)) == 0)
		{
			denied = landlock_allow(dirpath,
						IR0_EARLY_LANDLOCK_FS_READ_DIR);
			if (denied < 0)
				return denied;
		}
	}
	{
		const char *dirpath = "/";
		unsigned base;
		int fsid = pipe_id_of_fd(fd);

		if (node == IR0_EARLY_ROOTFS_DEV)
			dirpath = "/dev";
		else if (node == IR0_EARLY_ROOTFS_TMP)
			dirpath = "/tmp";
		else if (node == IR0_EARLY_ROOTFS_USERDIR && fsid >= 0 &&
			 fsid < RAMTAB_MAX && ramtab_table()[fsid].used)
			dirpath = ramtab_table()[fsid].path;
		base = ir0_early_rootfs_dir_count(node);
		if (node == IR0_EARLY_ROOTFS_USERDIR && base == 0)
			base = 2;
		if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
			base++;
		total = base + (unsigned)ramtab_child_count(dirpath);
		idx = slot_from_fd(fd);
		if (idx < 0)
			return -EBADF;
		{
			uint64_t *dpos = pos_of_fd(fd);

			if (!dpos)
				return -EBADF;
			if (count == 0)
				return -EINVAL;
			while (*dpos < total)
			{
				struct ir0_early_dirent_spec spec;
				unsigned reclen;
				unsigned i;
				unsigned syn = ir0_early_rootfs_dir_count(node);

				if (node == IR0_EARLY_ROOTFS_USERDIR && syn == 0)
					syn = 2;
				if (*dpos < syn)
				{
					if (node == IR0_EARLY_ROOTFS_USERDIR &&
					    ir0_early_rootfs_dir_count(node) == 0)
					{
						spec.name = (*dpos == 0) ? "." : "..";
						spec.ino = (*dpos == 0) ? 18 : 1;
						spec.type = IR0_EARLY_DT_DIR;
					}
					else
						spec = ir0_early_rootfs_dir_entry(node,
								 (unsigned)*dpos);
				}
				else if (node == IR0_EARLY_ROOTFS_DEV && vda_present() &&
					 *dpos == syn)
				{
					spec.name = "vda";
					spec.ino = 8;
					spec.type = IR0_EARLY_DT_BLK;
				}
				else
				{
					unsigned skip = syn;

					if (node == IR0_EARLY_ROOTFS_DEV && vda_present())
						skip++;
					if (!ramtab_child_at(dirpath,
							 (int)(*dpos - skip),
							 &spec))
						break;
				}
				if (!spec.name)
					break;
				reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino,
							(int64_t)(*dpos + 1),
							spec.type, spec.name);
				if (reclen == 0)
					return -EINVAL;
				if ((uint64_t)reclen > count - written)
					break;
				if (tmp_used + reclen > sizeof(tmp))
					break;
				for (i = 0; i < reclen; i++)
					tmp[tmp_used + i] = rec[i];
				tmp_used += reclen;
				written += reclen;
				(*dpos)++;
			}
		}
		if (written == 0)
			return 0;
		if (!g_user || !g_user->copy_to_user ||
		    g_user->copy_to_user(buffer, tmp, written) != 0)
			return -EFAULT;
		return (int64_t)written;
	}
}

int early_rootfs_dents_ready(void)
{
	uint8_t rec[32];
	unsigned i;
	unsigned n;

	if (!g_ready)
		return 0;
	n = ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_ROOT);
	if (n != 5 || ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_DEV) != 6 ||
	    ir0_early_rootfs_dir_count(IR0_EARLY_ROOTFS_TMP) != 2)
		return 0;
	for (i = 0; i < n; i++)
	{
		struct ir0_early_dirent_spec spec;
		unsigned reclen;

		spec = ir0_early_rootfs_dir_entry(IR0_EARLY_ROOTFS_ROOT, i);
		if (!spec.name)
			return 0;
		reclen = ir0_early_dirent64_pack(rec, sizeof(rec), spec.ino,
						(int64_t)(i + 1), spec.type,
						spec.name);
		if (reclen < 20)
			return 0;
	}
	return 1;
}

int64_t early_rootfs_getcwd(uint64_t buffer, uint64_t size)
{
	const char *cwd;
	char task_cwd[256];
	uint64_t n;
	int got;

	if (!g_ready)
		return -ENOENT;
	if (g_cwd_ops && g_cwd_ops->get)
	{
		got = g_cwd_ops->get(task_cwd, sizeof(task_cwd));
		if (got > 0)
		{
			cwd = task_cwd;
			n = (uint64_t)got;
			if (size < n)
				return -ERANGE;
			if (!g_user || !g_user->copy_to_user ||
			    g_user->copy_to_user(buffer, cwd, n) != 0)
				return -EFAULT;
			g_cwd_used = 1;
			return (int64_t)n;
		}
	}
	cwd = early_cwd_string();
	n = 0;
	while (cwd[n] != '\0')
		n++;
	n++;
	if (size < n)
		return -ERANGE;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buffer, cwd, n) != 0)
		return -EFAULT;
	return (int64_t)n;
}

int64_t early_rootfs_lseek(int fd, int64_t offset, int whence)
{
	enum ir0_early_rootfs_node node;
	uint64_t *pos;
	int64_t base;
	int64_t next;
	uint64_t limit;

	if (vf_of_fd(fd) && g_bpath && g_bpath->lseek)
	{
		off_t got;

		got = g_bpath->lseek(vf_of_fd(fd), (off_t)offset, whence);
		if (got < 0)
			return (int64_t)got;
		g_bpath_used = 1;
		return (int64_t)got;
	}
	node = node_of_fd(fd);
	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_CONSOLE || node == IR0_EARLY_ROOTFS_TTY ||
	    node == IR0_EARLY_ROOTFS_PIPE_R || node == IR0_EARLY_ROOTFS_PIPE_W ||
	    node == IR0_EARLY_ROOTFS_EVENTFD || node == IR0_EARLY_ROOTFS_TIMERFD ||
	    node == IR0_EARLY_ROOTFS_EPOLL || node == IR0_EARLY_ROOTFS_SOCK ||
	    node == IR0_EARLY_ROOTFS_FIFO)
		return -ESPIPE;
	pos = pos_of_fd(fd);
	if (!pos)
		return -EBADF;
	if (node == IR0_EARLY_ROOTFS_INIT)
		limit = g_init_vis;
	else if (node == IR0_EARLY_ROOTFS_VDA)
		limit = vda_bytes();
	else if (node == IR0_EARLY_ROOTFS_MEMFD)
	{
		int mid = pipe_id_of_fd(fd);

		limit = (mid >= 0 && mid < MEMFD_MAX && g_memfd[mid].used) ?
				g_memfd[mid].len :
				0;
	}
	else if (node == IR0_EARLY_ROOTFS_FILE)
	{
		int fsid = ramtab_body(pipe_id_of_fd(fd));

		limit = (fsid >= 0 && fsid < RAMTAB_MAX && ramtab_table()[fsid].used) ?
				ramtab_table()[fsid].size :
				0;
	}
	else if (node_is_dir(node))
		limit = ir0_early_rootfs_dir_count(node);
	else
		limit = 0;
	if (whence == SEEK_SET)
		base = 0;
	else if (whence == SEEK_CUR)
		base = (int64_t)*pos;
	else if (whence == SEEK_END)
		base = (int64_t)limit;
	else
		return -EINVAL;
	next = base + offset;
	if (next < 0)
		return -EINVAL;
	*pos = (uint64_t)next;
	return next;
}

int64_t early_rootfs_fchdir(int fd)
{
	enum ir0_early_rootfs_node node = node_of_fd(fd);

	if (node == IR0_EARLY_ROOTFS_NONE)
		return -EBADF;
	if (!node_is_dir(node))
		return -ENOTDIR;
	g_cwd = node;
	if (node == IR0_EARLY_ROOTFS_USERDIR)
	{
		int fsid = pipe_id_of_fd(fd);
		unsigned i;

		if (fsid >= 0 && fsid < RAMTAB_MAX && ramtab_table()[fsid].used)
		{
			for (i = 0; ramtab_table()[fsid].path[i] != '\0' &&
				    i + 1 < sizeof(g_cwd_path); i++)
				g_cwd_path[i] = ramtab_table()[fsid].path[i];
			g_cwd_path[i] = '\0';
		}
	}
	else if (node == IR0_EARLY_ROOTFS_DEV)
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = 'd';
		g_cwd_path[2] = 'e';
		g_cwd_path[3] = 'v';
		g_cwd_path[4] = '\0';
	}
	else if (node == IR0_EARLY_ROOTFS_TMP)
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = 't';
		g_cwd_path[2] = 'm';
		g_cwd_path[3] = 'p';
		g_cwd_path[4] = '\0';
	}
	else
	{
		g_cwd_path[0] = '/';
		g_cwd_path[1] = '\0';
	}
	early_publish_cwd();
	return 0;
}
