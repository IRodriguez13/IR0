/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/erfs_bind.c
 * Description: Early rootfs bind — init blob, product path, block/CPIO bootstrap.
 */

#include "erfs_internal.h"

static void early_unix_rehome(int old_id, int new_id)
{
	int i;

	for (i = 0; i < UNIX_MAX; i++)
	{
		if (g_unix[i].used && g_unix[i].bound_fs == old_id)
			g_unix[i].bound_fs = new_id;
	}
}

static void reset_stdio(void)
{
	g_stdio_open[0] = 1;
	g_stdio_open[1] = 1;
	g_stdio_open[2] = 1;
	g_stdio_node[0] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_node[1] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_node[2] = IR0_EARLY_ROOTFS_CONSOLE;
	g_stdio_pos[0] = 0;
	g_stdio_pos[1] = 0;
	g_stdio_pos[2] = 0;
	g_stdio_flags[0] = O_RDWR;
	g_stdio_flags[1] = O_RDWR;
	g_stdio_flags[2] = O_RDWR;
	g_stdio_cloexec[0] = 0;
	g_stdio_cloexec[1] = 0;
	g_stdio_cloexec[2] = 0;
	g_cwd = IR0_EARLY_ROOTFS_ROOT;
	g_chroot = IR0_EARLY_ROOTFS_ROOT;
}

int ir0_product_path_is_tmp(const char *path)
{
	unsigned i;

	if (!path)
		return 0;
	for (i = 0; IR0_PRODUCT_PATH_TMP_PREF[i] != '\0'; i++)
	{
		if (path[i] != IR0_PRODUCT_PATH_TMP_PREF[i])
			return 0;
	}
	return path[i] != '\0';
}

void early_rootfs_bind_product_path(const struct ir0_product_path_ops *ops)
{
	g_product_path = ops;
	g_product_path_used = 0;
}

const struct ir0_product_path_ops *ir0_product_path_bound(void)
{
	return g_product_path;
}

int early_rootfs_product_path_used(void)
{
	return g_product_path_used;
}

int product_path_live(void)
{
	stat_t st;

	if (!g_product_path || !g_product_path->stat)
		return 0;
	return g_product_path->stat("/tmp", &st) == 0;
}

enum ir0_early_rootfs_node product_lookup(const char *path)
{
	stat_t st;

	if (!product_path_live() || !ir0_product_path_is_tmp(path))
		return IR0_EARLY_ROOTFS_NONE;
	if (g_product_path->stat(path, &st) != 0)
		return IR0_EARLY_ROOTFS_NONE;
	g_product_path_used = 1;
	if ((st.st_mode & S_IFMT) == S_IFDIR)
		return IR0_EARLY_ROOTFS_USERDIR;
	return IR0_EARLY_ROOTFS_FILE;
}

int64_t product_attach_open(int fd, const char *path, int flags)
{
	struct vfs_file *vf = 0;
	int sidx;
	int rc;

	if (!g_product_path || !g_product_path->open)
		return -ENODEV;
	rc = g_product_path->open(path, flags, 0644, &vf);
	if (rc < 0)
		return rc;
	sidx = slot_from_fd(fd);
	if (sidx < 0)
	{
		if (g_product_path->put && vf)
			(void)g_product_path->put(vf);
		return -EBADF;
	}
	g_slots[sidx].vf = vf;
	g_product_path_used = 1;
	return 0;
}

struct vfs_file *vf_of_fd(int fd)
{
	int idx = slot_from_fd(fd);

	if (idx < 0)
		return 0;
	return g_slots[idx].vf;
}

int64_t product_xfer(int fd, uint64_t buffer, uint64_t count, int write)
{
	struct vfs_file *vf = vf_of_fd(fd);
	uint8_t tmp[256];
	int n;

	if (!vf || !g_product_path)
		return -ENODEV;
	if (count == 0)
		return 0;
	n = (int)(count > sizeof(tmp) ? sizeof(tmp) : count);
	if (write)
	{
		if (!g_product_path->write)
			return -ENODEV;
		if (!g_user || !g_user->copy_from_user ||
		    g_user->copy_from_user(tmp, buffer, (uint64_t)n) != 0)
			return -EFAULT;
		n = g_product_path->write(vf, tmp, (size_t)n);
	}
	else
	{
		if (!g_product_path->read)
			return -ENODEV;
		n = g_product_path->read(vf, tmp, (size_t)n);
		if (n > 0 && (!g_user || !g_user->copy_to_user ||
			      g_user->copy_to_user(buffer, tmp, (uint64_t)n) != 0))
			return -EFAULT;
	}
	if (n < 0)
		return n;
	g_product_path_used = 1;
	return n;
}

void early_rootfs_bind(const uint8_t *init_blob, uint64_t init_len,
		       const struct early_user_services *user)
{
	unsigned i;

	g_blob = init_blob;
	g_blob_len = init_len;
	g_init_vis = init_len;
	g_user = user;
	for (i = 0; i < ROOTFS_MAX_OPEN; i++)
	{
		g_slots[i].used = 0;
		g_slots[i].node = IR0_EARLY_ROOTFS_NONE;
		g_slots[i].pos = 0;
		g_slots[i].flags = 0;
		g_slots[i].cloexec = 0;
		g_slots[i].pipe_id = -1;
		g_slots[i].vf = 0;
	}
	early_anonfd_reset();
	early_unix_reset();
	g_clock_mono = 0;
	g_clock_hz = 0;
	g_clock_realtime_off = 0;
	ramtab_reset();
	ramtab_set_rehome_observer(early_unix_rehome);
	for (i = 0; i < XA_MAX; i++)
		g_xattr[i].used = 0;
	for (i = 0; i < RULESET_MAX; i++)
	{
		g_ruleset[i].used = 0;
		g_ruleset[i].refs = 0;
		g_ruleset[i].nrules = 0;
	}
	g_ll_on = 0;
	g_ll_handled = 0;
	g_ll_n = 0;
	g_cpio_ready = 0;
	g_cwd_path[0] = '/';
	g_cwd_path[1] = '\0';
	g_chroot_path[0] = '/';
	g_chroot_path[1] = '\0';
	reset_stdio();
	g_exec_pending = 0;
	g_exec_owner = 0;
	g_abstract_ok = 0;
	g_init_from_block = 0;
	g_ready = (init_blob != 0 && init_len > 0 && user &&
		   user->copy_from_user && user->copy_to_user);
}

const uint8_t *early_rootfs_init_blob(uint64_t *length)
{
	if (length)
		*length = g_blob_len;
	return g_blob;
}

int early_rootfs_init_from_block(void)
{
	return g_init_from_block;
}

int early_rootfs_cpio_ready(void)
{
	return g_cpio_ready;
}

int early_rootfs_try_bind_block(const char *name, uint8_t *buffer,
				uint64_t buffer_len,
				const struct early_user_services *user)
{
	uint8_t sector[512];
	uint64_t phoff;
	uint16_t phnum;
	uint16_t phentsize;
	uint64_t header_bytes;
	uint64_t image_bytes;
	uint64_t sectors;
	uint64_t i;

	if (!name || !buffer || buffer_len < 512 || !user)
		return -EINVAL;
	if (!ir0_block_name_is_present(name))
		return -ENOENT;
	if (ir0_block_read_by_name(name, 0, 1, sector) != 0)
		return -EIO;
	if (ir0_early_cpio_magic(sector))
	{
		uint64_t bytes = vda_bytes();
		uint64_t load;
		const uint8_t *init_data = 0;
		uint64_t init_len = 0;
		int installed;

		if (bytes > buffer_len)
			bytes = buffer_len;
		load = (bytes + 511UL) / 512UL;
		for (i = 0; i < load; i++)
		{
			if (ir0_block_read_by_name(name, i, 1,
						   buffer + i * 512UL) != 0)
				return -EIO;
		}
		if (erfs_cpio_find_init(buffer, bytes, &init_data, &init_len) != 0)
			return -ENOEXEC;
		early_rootfs_bind(init_data, init_len, user);
		g_init_from_block = 1;
		g_cpio_buf = buffer;
		g_cpio_len = bytes;
		installed = erfs_cpio_install_members(buffer, bytes);
		if (installed < 0)
			return installed;
		g_cpio_ready = installed > 0;
		return 0;
	}
	if (sector[0] != 0x7f || sector[1] != 'E' || sector[2] != 'L' ||
	    sector[3] != 'F' || sector[4] != 2 || sector[5] != 1)
		return -ENOEXEC;
	for (i = 0; i < 512; i++)
		buffer[i] = sector[i];
	phoff = ir0_early_le64(sector + 32);
	phentsize = ir0_early_le16(sector + 54);
	phnum = ir0_early_le16(sector + 56);
	header_bytes = 64;
	if (phoff && phentsize && phnum)
		header_bytes = phoff + (uint64_t)phnum * phentsize;
	if (header_bytes > buffer_len)
		return -E2BIG;
	if (header_bytes > 512)
	{
		sectors = (header_bytes + 511UL) / 512UL;
		for (i = 1; i < sectors; i++)
		{
			if (ir0_block_read_by_name(name, i, 1,
						   buffer + i * 512UL) != 0)
				return -EIO;
		}
	}
	image_bytes = ir0_early_elf64_image_bytes(buffer, buffer_len);
	if (image_bytes == 0)
		return -ENOEXEC;
	if (image_bytes > buffer_len)
		return -E2BIG;
	sectors = (image_bytes + 511UL) / 512UL;
	for (i = (header_bytes + 511UL) / 512UL; i < sectors; i++)
	{
		if (ir0_block_read_by_name(name, i, 1, buffer + i * 512UL) != 0)
			return -EIO;
	}
	early_rootfs_bind(buffer, image_bytes, user);
	g_init_from_block = 1;
	return 0;
}
