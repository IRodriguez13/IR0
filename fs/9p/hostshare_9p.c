/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: hostshare_9p.c
 * Description: VFS fstype "9p" backed by virtio-9p (QEMU -virtfs host share).
 */

#include <ir0/kmem.h>
#include <fs/vfs.h>
#include <ir0/errno.h>
#include <ir0/inode.h>
#include <ir0/virtio_9p.h>
#include <string.h>
#include <stddef.h>

static char g_mount_point[VFS_PATH_MAX];
static int g_mounted;

static const char *rel_of(const char *path)
{
	size_t mlen;

	if (!path || !g_mounted)
		return NULL;
	mlen = strlen(g_mount_point);
	if (strncmp(path, g_mount_point, mlen) != 0)
		return NULL;
	if (path[mlen] == '\0')
		return "";
	if (path[mlen] != '/')
		return NULL;
	return path + mlen + 1;
}

/*
 * The server's qid.path names the file; folded to ino_t like Linux v9fs
 * does where ino_t is 32 bits (QEMU derives it from the host inode).
 */
static ino_t hs_qid_ino(uint64_t qid)
{
	uint32_t ino = (uint32_t)(qid ^ (qid >> 32));

	return (ino_t)(ino ? ino : 1);
}

static void hs_fill_stat(stat_t *buf, uint64_t size, uint32_t mode,
			 uint64_t qid, uint64_t nlink)
{
	memset(buf, 0, sizeof(*buf));
	buf->st_ino = hs_qid_ino(qid);
	buf->st_nlink = (nlink_t)nlink;
	if (mode != 0)
		buf->st_mode = (mode_t)mode;
	else
		buf->st_mode = S_IFREG | 0755;
	if (S_ISLNK(buf->st_mode))
	{
		/* Keep host symlink type; liberal perms for path checks. */
		buf->st_mode = S_IFLNK | 0777;
	}
	else if (S_ISDIR(buf->st_mode))
		buf->st_mode = S_IFDIR | 0777;
	else if ((buf->st_mode & 0777) == 0)
		buf->st_mode |= 0755;
	buf->st_uid = 0;
	buf->st_gid = 0;
	buf->st_size = (off_t)size;
}

static int hs_stat(const char *path, stat_t *buf)
{
	uint64_t size = 0;
	uint64_t qid = 0;
	uint32_t mode = 0;
	int rc;
	const char *rel = rel_of(path);

	if (!buf)
		return -EFAULT;
	if (!rel)
		return -ENOENT;
	if (rel[0] == '\0')
	{
		memset(buf, 0, sizeof(*buf));
		buf->st_mode = S_IFDIR | 0777;
		return 0;
	}
	rc = virtio_9p_stat_file(rel, &size, &mode, &qid);
	if (rc < 0)
		return rc;
	hs_fill_stat(buf, size, mode, qid, 1);
	return 0;
}

static int hs_mkdir_one(const char *rel, mode_t mode)
{
	uint64_t size = 0;
	uint32_t stmode = 0;
	int rc;

	if (!rel || !rel[0])
		return -EINVAL;
	rc = virtio_9p_stat_file(rel, &size, &stmode, NULL);
	if (rc == 0)
	{
		if (S_ISDIR(stmode))
			return 0;
		return -EEXIST;
	}
	return virtio_9p_mkdir(rel, (uint32_t)(mode ? (mode & 0777) : 0755));
}

static int hs_mkdir(const char *path, mode_t mode)
{
	const char *rel = rel_of(path);
	char tmp[VFS_PATH_MAX];
	size_t i;

	if (!rel || !rel[0])
		return -EINVAL;
	if (strlen(rel) >= sizeof(tmp))
		return -ENAMETOOLONG;
	memcpy(tmp, rel, strlen(rel) + 1);

	/* mkdir -p: create each path component */
	for (i = 0; tmp[i]; i++)
	{
		if (tmp[i] != '/')
			continue;
		tmp[i] = '\0';
		if (tmp[0])
		{
			int rc = hs_mkdir_one(tmp, mode);

			if (rc < 0 && rc != -EEXIST)
				return rc;
		}
		tmp[i] = '/';
	}
	return hs_mkdir_one(tmp, mode);
}

static int hs_create(const char *path, mode_t mode)
{
	const char *rel = rel_of(path);
	(void)mode;
	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_write_file(rel, "", 0);
}

static int hs_read(const char *path, void *buf, size_t count, size_t *bytes_read,
		   off_t offset)
{
	size_t got = 0;
	int rc;
	const char *rel = rel_of(path);

	if (bytes_read)
		*bytes_read = 0;
	if (!rel || !rel[0])
		return -EISDIR;
	if (offset < 0)
		return -EINVAL;
	rc = virtio_9p_read_at(rel, buf, count, (uint64_t)offset, &got);
	if (rc < 0)
		return rc;
	if (bytes_read)
		*bytes_read = got;
	return 0;
}

static int hs_write(const char *path, const void *buf, size_t count,
		    size_t *bytes_written, off_t offset)
{
	const char *rel = rel_of(path);
	int rc;

	if (bytes_written)
		*bytes_written = 0;
	if (!rel || !rel[0])
		return -EISDIR;
	if (offset < 0)
		return -EINVAL;
	if (offset == 0)
		rc = virtio_9p_write_file(rel, buf, count);
	else
		rc = virtio_9p_write_at(rel, buf, count, (uint64_t)offset);
	if (rc < 0)
		return rc;
	if (bytes_written)
		*bytes_written = count;
	return 0;
}

static int hs_truncate(const char *path, size_t length)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EISDIR;
	if (length == 0)
		return virtio_9p_write_file(rel, "", 0);
	return virtio_9p_truncate(rel, (uint64_t)length);
}

static int hs_readdir(const char *path, struct vfs_dirent *entries, int max)
{
	const char *rel = rel_of(path);
	/*
	 * On the stack this array made the frame 4 KiB, on a getdents chain
	 * that already spent most of the kernel stack before reaching 9p.
	 */
	virtio_9p_dirent_t *buf;
	int n;
	int i;
	int out = 0;

	if (!rel || !entries || max <= 0)
		return -EINVAL;

	buf = kcalloc(64, sizeof(*buf));
	if (!buf)
		return -ENOMEM;

	n = virtio_9p_readdir(rel, buf, 64);
	if (n < 0)
	{
		kfree(buf);
		return n;
	}
	for (i = 0; i < n && out < max; i++)
	{
		strncpy(entries[out].name, buf[i].name, sizeof(entries[out].name) - 1);
		entries[out].name[sizeof(entries[out].name) - 1] = '\0';
		entries[out].type = buf[i].type;
		out++;
	}
	kfree(buf);
	return out;
}

static int hs_unlink(const char *path)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_unlink(rel, 0);
}

static int hs_rmdir(const char *path)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0])
		return -EINVAL;
	return virtio_9p_unlink(rel, 1);
}

static int hs_link(const char *oldpath, const char *newpath)
{
	const char *old_rel = rel_of(oldpath);
	const char *new_rel = rel_of(newpath);

	if (!old_rel || !old_rel[0] || !new_rel || !new_rel[0])
		return -EINVAL;
	return virtio_9p_link(old_rel, new_rel);
}

static int hs_rename(const char *oldpath, const char *newpath)
{
	const char *old_rel = rel_of(oldpath);
	const char *new_rel = rel_of(newpath);

	if (!old_rel || !old_rel[0] || !new_rel || !new_rel[0])
		return -EINVAL;
	return virtio_9p_rename(old_rel, new_rel);
}

static int hs_symlink(const char *target, const char *linkpath)
{
	const char *rel = rel_of(linkpath);

	if (!target || !rel || !rel[0])
		return -EINVAL;
	return virtio_9p_symlink(rel, target);
}

static int hs_readlink(const char *path, char *buf, size_t buflen)
{
	const char *rel = rel_of(path);

	if (!rel || !rel[0] || !buf || buflen == 0)
		return -EINVAL;
	return virtio_9p_readlink(rel, buf, buflen);
}

/* vfs_inode_ops: a file through the fid its inode holds (Linux v9fs). */
static int hs_iopen(struct inode *inode, const char *path)
{
	const char *rel = rel_of(path);
	uint32_t fid;
	int rc;

	if (!rel || !rel[0])
		return -EISDIR;
	rc = virtio_9p_open(rel, &fid);
	if (rc < 0)
		return rc;
	inode->i_private = (void *)(uintptr_t)fid;
	return 0;
}

static uint32_t hs_fid(const struct inode *inode)
{
	return (uint32_t)(uintptr_t)inode->i_private;
}

static int hs_istat(struct inode *inode, stat_t *buf)
{
	uint64_t size = 0;
	uint64_t qid = 0;
	uint64_t nlink = 0;
	uint32_t mode = 0;
	int rc = virtio_9p_fid_getattr(hs_fid(inode), &size, &mode, &qid, &nlink);

	if (rc < 0)
		return rc;
	hs_fill_stat(buf, size, mode, qid, nlink);
	return 0;
}

static int hs_iread(struct inode *inode, void *buf, size_t count,
		    size_t *done, off_t offset)
{
	size_t got = 0;
	int rc;

	if (offset < 0)
		return -EINVAL;
	rc = virtio_9p_fid_read(hs_fid(inode), buf, count, (uint64_t)offset, &got);
	if (rc < 0)
		return rc;
	if (done)
		*done = got;
	return 0;
}

static int hs_iwrite(struct inode *inode, const void *buf, size_t count,
		     size_t *done, off_t offset)
{
	int rc;

	if (offset < 0)
		return -EINVAL;
	rc = virtio_9p_fid_write(hs_fid(inode), buf, count, (uint64_t)offset);
	if (rc < 0)
		return rc;
	if (done)
		*done = count;
	return 0;
}

static int hs_itruncate(struct inode *inode, size_t length)
{
	return virtio_9p_fid_truncate(hs_fid(inode), (uint64_t)length);
}

static void hs_irelease(struct inode *inode)
{
	(void)virtio_9p_close(hs_fid(inode));
}

static const struct vfs_inode_ops hs_inode_ops = {
	.open_inode = hs_iopen,
	.stat = hs_istat,
	.read = hs_iread,
	.write = hs_iwrite,
	.truncate = hs_itruncate,
	.release = hs_irelease,
};

static struct vfs_ops hs_ops = {
	.stat = hs_stat,
	.mkdir = hs_mkdir,
	.create = hs_create,
	.unlink = hs_unlink,
	.rmdir = hs_rmdir,
	.link = hs_link,
	.rename = hs_rename,
	.symlink = hs_symlink,
	.readlink = hs_readlink,
	.read = hs_read,
	.write = hs_write,
	.truncate = hs_truncate,
	.readdir = hs_readdir,
	.iops = &hs_inode_ops,
};

static int hs_mount(const char *dev, const char *dir)
{
	(void)dev;
	if (!dir || !virtio_9p_ready())
		return -ENODEV;
	strncpy(g_mount_point, dir, sizeof(g_mount_point) - 1);
	g_mount_point[sizeof(g_mount_point) - 1] = '\0';
	g_mounted = 1;
	return 0;
}

static int hs_umount(const char *dir)
{
	(void)dir;
	g_mounted = 0;
	g_mount_point[0] = '\0';
	return 0;
}

static int hs_statfs(const char *path, struct ir0_statfs *buf)
{
	struct virtio_9p_statfs h;

	(void)path;
	buf->f_type = IR0_9P_MAGIC;
	buf->f_bsize = 4096;
	buf->f_frsize = 4096;
	buf->f_blocks = 0;
	buf->f_bfree = 0;
	buf->f_bavail = 0;
	buf->f_namelen = 255;

	/*
	 * Ask the host. Falling back to the zeros above keeps df working
	 * against a server without Tstatfs instead of failing the whole call.
	 */
	if (virtio_9p_statfs(&h) == 0 && h.bsize != 0)
	{
		buf->f_bsize = h.bsize;
		buf->f_frsize = h.bsize;
		buf->f_blocks = h.blocks;
		buf->f_bfree = h.bfree;
		buf->f_bavail = h.bavail;
		buf->f_files = h.files;
		buf->f_ffree = h.ffree;
		if (h.namelen)
			buf->f_namelen = h.namelen;
	}
	return 0;
}

struct vfs_fstype v9fs_fs_type = {
	.name = "9p",
	.ops = &hs_ops,
	.mount = hs_mount,
	.umount = hs_umount,
	.statfs = hs_statfs,
	.next = NULL,
};
