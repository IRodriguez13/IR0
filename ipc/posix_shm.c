/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: posix_shm.c
 * Description: /dev/shm name table backed by ir0_memfd (shm_open path).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/posix_shm.h>
#include <ir0/memfd.h>
#include <ir0/errno.h>
#include <ir0/fcntl.h>
#include <ir0/kmem.h>
#include <ir0/process.h>
#include <string.h>

#define POSIX_SHM_MAX 16
#define POSIX_SHM_PREFIX "/dev/shm/"
#define POSIX_SHM_PREFIX_LEN 9

struct posix_shm_slot
{
	int in_use;
	char name[64];
	struct ir0_memfd *m;
};

static struct posix_shm_slot g_shm[POSIX_SHM_MAX];
static int g_dir_ready;

void posix_shm_ensure_dir(void)
{
	g_dir_ready = 1;
}

int posix_shm_path_is(const char *path)
{
	return path && strncmp(path, POSIX_SHM_PREFIX, POSIX_SHM_PREFIX_LEN) == 0 &&
	       path[POSIX_SHM_PREFIX_LEN] != '\0' &&
	       !strchr(path + POSIX_SHM_PREFIX_LEN, '/');
}

static int shm_name_from_path(const char *path, char *out, size_t out_sz)
{
	const char *n;
	size_t len;

	if (!posix_shm_path_is(path))
		return -ENOENT;
	n = path + POSIX_SHM_PREFIX_LEN;
	len = strlen(n);
	if (len == 0 || len >= out_sz)
		return -ENAMETOOLONG;
	memcpy(out, n, len + 1);
	return 0;
}

static struct posix_shm_slot *shm_find(const char *name)
{
	int i;

	for (i = 0; i < POSIX_SHM_MAX; i++)
	{
		if (g_shm[i].in_use && strcmp(g_shm[i].name, name) == 0)
			return &g_shm[i];
	}
	return NULL;
}

static struct posix_shm_slot *shm_alloc_slot(void)
{
	int i;

	for (i = 0; i < POSIX_SHM_MAX; i++)
	{
		if (!g_shm[i].in_use)
			return &g_shm[i];
	}
	return NULL;
}

int64_t posix_shm_try_open(const char *path, int flags, mode_t mode)
{
	char name[64];
	struct posix_shm_slot *slot;
	struct ir0_memfd *m;
	int fd;
	int creat = (flags & O_CREAT) != 0;
	int excl = (flags & O_EXCL) != 0;
	int cloexec = (flags & O_CLOEXEC) != 0;
	int open_flags;

	(void)mode;
	posix_shm_ensure_dir();
	if (shm_name_from_path(path, name, sizeof(name)) != 0)
		return -ENOENT; /* not a shm path — caller continues */

	/* Distinguish "not our path" vs errors: use special sentinel.
	 * Caller checks: if path starts with /dev/shm/ then honor return.
	 */
	slot = shm_find(name);
	if (slot)
	{
		if (creat && excl)
			return -EEXIST;
		ir0_memfd_acquire(slot->m);
		open_flags = flags & O_ACCMODE;
		if (open_flags == 0)
			open_flags = O_RDWR;
		fd = ir0_memfd_install_fd(slot->m, open_flags, cloexec);
		if (fd < 0)
			ir0_memfd_release(slot->m);
		return fd;
	}
	if (!creat)
		return -ENOENT;
	slot = shm_alloc_slot();
	if (!slot)
		return -ENOMEM;
	m = ir0_memfd_alloc();
	if (!m)
		return -ENOMEM;
	memset(slot, 0, sizeof(*slot));
	slot->in_use = 1;
	memcpy(slot->name, name, sizeof(slot->name) - 1);
	slot->m = m; /* holds alloc ref */
	ir0_memfd_acquire(m); /* fd ref */
	open_flags = flags & O_ACCMODE;
	if (open_flags == 0)
		open_flags = O_RDWR;
	fd = ir0_memfd_install_fd(m, open_flags, cloexec);
	if (fd < 0)
	{
		ir0_memfd_release(m); /* drop fd ref */
		ir0_memfd_release(m); /* drop slot ref */
		memset(slot, 0, sizeof(*slot));
		return fd;
	}
	return fd;
}

int posix_shm_try_unlink(const char *path)
{
	char name[64];
	struct posix_shm_slot *slot;

	if (shm_name_from_path(path, name, sizeof(name)) != 0)
		return -ENOENT;
	slot = shm_find(name);
	if (!slot)
		return -ENOENT;
	ir0_memfd_release(slot->m);
	memset(slot, 0, sizeof(*slot));
	return 0;
}
