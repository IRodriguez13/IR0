/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_mount.c
 * Description: staged rootfs statmount/listmount and mount-id request parsing.
 */

#include "erfs_internal.h"

static int copy_mnt_req(uint64_t req, uint64_t *mnt_id, uint32_t *mnt_fd,
			uint64_t *param, unsigned flags)
{
	uint32_t size = 0;
	uint32_t spare = 0;
	uint64_t id = 0;
	uint64_t prm = 0;

	if (!req || !g_user || !g_user->copy_from_user)
		return -EFAULT;
	if (g_user->copy_from_user(&size, req, 4) != 0)
		return -EFAULT;
	if (size > 4096)
		return -E2BIG;
	if (size < IR0_EARLY_MNT_ID_REQ_VER0)
		return -EINVAL;
	if (g_user->copy_from_user(&spare, req + 4, 4) != 0 ||
	    g_user->copy_from_user(&id, req + 8, 8) != 0 ||
	    g_user->copy_from_user(&prm, req + 16, 8) != 0)
		return -EFAULT;
	if (size > IR0_EARLY_MNT_ID_REQ_VER0)
	{
		uint64_t off;
		uint8_t byte;

		for (off = IR0_EARLY_MNT_ID_REQ_VER0; off < size; off++)
		{
			if (g_user->copy_from_user(&byte, req + off, 1) != 0)
				return -EFAULT;
			if (byte)
				return -E2BIG;
		}
	}
	if (flags & IR0_EARLY_STATMOUNT_BY_FD)
	{
		if (id)
			return -EINVAL;
	}
	else if (id != IR0_EARLY_LSMT_ROOT && id <= 0x80000000ULL)
		return -EINVAL;
	*mnt_id = id;
	*mnt_fd = spare;
	*param = prm;
	return 0;
}

int64_t early_rootfs_statmount(uint64_t req, uint64_t buf, uint64_t bufsize,
			       unsigned flags)
{
	uint64_t mnt_id = 0;
	uint32_t mnt_fd = 0;
	uint64_t mask = 0;
	uint8_t out[IR0_EARLY_STATMOUNT_FIXED + 16];
	uint32_t size;
	uint64_t written;
	unsigned i;
	int ret;

	if (flags & ~IR0_EARLY_STATMOUNT_BY_FD)
		return -EINVAL;
	ret = copy_mnt_req(req, &mnt_id, &mnt_fd, &mask, flags);
	if (ret < 0)
		return ret;
	if (flags & IR0_EARLY_STATMOUNT_BY_FD)
	{
		if (node_of_fd((int)mnt_fd) == IR0_EARLY_ROOTFS_NONE)
			return -EBADF;
	}
	else if (mnt_id != IR0_EARLY_MNT_ID && mnt_id != IR0_EARLY_LSMT_ROOT)
		return -ENOENT;
	if (!buf)
		return -EFAULT;
	if (bufsize < IR0_EARLY_STATMOUNT_FIXED + 10)
		return -EOVERFLOW;
	for (i = 0; i < sizeof(out); i++)
		out[i] = 0;
	written = IR0_EARLY_STATMOUNT_SB_BASIC | IR0_EARLY_STATMOUNT_MNT_BASIC |
		  IR0_EARLY_STATMOUNT_MNT_ROOT | IR0_EARLY_STATMOUNT_MNT_POINT |
		  IR0_EARLY_STATMOUNT_FS_TYPE;
	if (mask)
		written &= mask;
	size = (uint32_t)(IR0_EARLY_STATMOUNT_FIXED + 10);
	out[0] = (uint8_t)size;
	out[1] = (uint8_t)(size >> 8);
	{
		uint64_t magic = IR0_TMPFS_MAGIC;
		uint64_t mid = IR0_EARLY_MNT_ID;

		out[8] = (uint8_t)written;
		out[9] = (uint8_t)(written >> 8);
		out[20] = 1;
		for (i = 0; i < 8; i++)
			out[24 + i] = (uint8_t)(magic >> (8 * i));
		for (i = 0; i < 8; i++)
			out[40 + i] = (uint8_t)(mid >> (8 * i));
		for (i = 0; i < 8; i++)
			out[48 + i] = (uint8_t)(mid >> (8 * i));
		out[36] = (uint8_t)IR0_EARLY_STATMOUNT_FIXED;
		out[37] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED >> 8);
		out[104] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED + 6);
		out[108] = (uint8_t)(IR0_EARLY_STATMOUNT_FIXED + 8);
	}
	out[IR0_EARLY_STATMOUNT_FIXED + 0] = 't';
	out[IR0_EARLY_STATMOUNT_FIXED + 1] = 'm';
	out[IR0_EARLY_STATMOUNT_FIXED + 2] = 'p';
	out[IR0_EARLY_STATMOUNT_FIXED + 3] = 'f';
	out[IR0_EARLY_STATMOUNT_FIXED + 4] = 's';
	out[IR0_EARLY_STATMOUNT_FIXED + 5] = 0;
	out[IR0_EARLY_STATMOUNT_FIXED + 6] = '/';
	out[IR0_EARLY_STATMOUNT_FIXED + 7] = 0;
	out[IR0_EARLY_STATMOUNT_FIXED + 8] = '/';
	out[IR0_EARLY_STATMOUNT_FIXED + 9] = 0;
	if (!g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(buf, out, size) != 0)
		return -EFAULT;
	return 0;
}

int64_t early_rootfs_listmount(uint64_t req, uint64_t ids, uint64_t nr,
			       unsigned flags)
{
	uint64_t mnt_id = 0;
	uint32_t spare = 0;
	uint64_t last = 0;
	uint64_t one = IR0_EARLY_MNT_ID;
	int ret;

	if (flags & ~IR0_EARLY_LISTMOUNT_REVERSE)
		return -EINVAL;
	if (nr > 1000000)
		return -EOVERFLOW;
	ret = copy_mnt_req(req, &mnt_id, &spare, &last, 0);
	if (ret < 0)
		return ret;
	if (mnt_id != IR0_EARLY_LSMT_ROOT && mnt_id != IR0_EARLY_MNT_ID)
		return -ENOENT;
	if (last != 0 && last != IR0_EARLY_MNT_ID)
		return 0;
	if (last == IR0_EARLY_MNT_ID)
		return 0;
	if (!nr)
		return 0;
	if (!ids || !g_user || !g_user->copy_to_user ||
	    g_user->copy_to_user(ids, &one, 8) != 0)
		return -EFAULT;
	return 1;
}
