/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - System Call Wrappers
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Safe wrappers for system calls with parameter validation
 */

#include "syscalls.h"
#include "syscalls_internal.h"
#include "process.h"
#include <ir0/memory/kmem.h>
#include <string.h>

/* Error codes */
#define EINVAL         22
#define EBADF          9
#define ENAMETOOLONG   36

typedef uint32_t mode_t;

int64_t safe_sys_write(int fd, const void *buf, size_t count)
{
	if (!buf || count == 0)
		return -EINVAL;

	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;

	return sys_write(fd, buf, count);
}

int64_t safe_sys_read(int fd, void *buf, size_t count)
{
	if (!buf || count == 0)
		return -EINVAL;

	if (fd < 0 || fd >= MAX_FDS_PER_PROCESS)
		return -EBADF;

	return sys_read(fd, buf, count);
}

int64_t safe_sys_open(const char *pathname, int flags, mode_t mode)
{
	if (!pathname || strlen(pathname) == 0)
		return -EINVAL;

	if (strlen(pathname) > 255)
		return -ENAMETOOLONG;

	return sys_open(pathname, flags, mode);
}


int64_t safe_sys_mkdir(const char *pathname, mode_t mode)
{
	if (!pathname || strlen(pathname) == 0)
		return -EINVAL;

	if (strlen(pathname) > 255)
		return -ENAMETOOLONG;

	/* Ensure reasonable permissions */
	mode &= 0777;

	return sys_mkdir(pathname, mode);
}

void *safe_kmalloc(size_t size)
{
	/* Max 1MB allocation */
	if (size == 0 || size > (1024 * 1024))
		return NULL;

	return kmalloc(size);
}
