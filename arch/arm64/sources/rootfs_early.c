/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: rootfs_early.c
 * Description: ARM64 adapter — bind embedded ET_EXEC blob + MMU usercopy
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "rootfs_early.h"
#include "mmu_early.h"

#include <stdint.h>
#include <ir0/boot_log.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/tlb.h>

extern const uint8_t embedded_elf_blob[];
extern const uint8_t embedded_elf_blob_end[];

#define DISK_INIT_MAX (128UL * 1024UL)

static uint8_t g_disk_init[DISK_INIT_MAX];

static int arm64_rootfs_copy_from_user(void *destination, uint64_t source,
				       uint64_t size)
{
	uint8_t *dst = destination;
	const volatile uint8_t *src;
	uint64_t i;

	if (!destination || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok_in_root((uint64_t)mm_current_root(),
					   source, size))
		return -1;
	src = (const volatile uint8_t *)(uintptr_t)source;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static int arm64_rootfs_copy_to_user(uint64_t destination, const void *source,
				     uint64_t size)
{
	volatile uint8_t *dst;
	const uint8_t *src = source;
	uint64_t i;

	if (!source || size == 0)
		return 0;
	if (!arm64_mmu_user_buf_ok_in_root((uint64_t)mm_current_root(),
					   destination, size))
		return -1;
	dst = (volatile uint8_t *)(uintptr_t)destination;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static const struct early_user_services g_arm64_rootfs_user = {
	.copy_from_user = arm64_rootfs_copy_from_user,
	.copy_to_user = arm64_rootfs_copy_to_user,
};

void arm64_rootfs_early_init(void)
{
	uint64_t len = (uint64_t)(embedded_elf_blob_end - embedded_elf_blob);

	if (early_rootfs_ready())
		return;
	if (early_rootfs_try_bind_block("vda", g_disk_init, sizeof(g_disk_init),
					&g_arm64_rootfs_user) == 0)
	{
		ir0_boot_smoke("ARM64_DISK_INIT_OK");
		if (early_rootfs_cpio_ready())
			ir0_boot_smoke("ARM64_CPIO_ROOT_OK");
	}
	else
		early_rootfs_bind(embedded_elf_blob, len, &g_arm64_rootfs_user);
	if (early_rootfs_ready())
		ir0_boot_smoke("ARM64_ROOTFS_OK");
	if (early_rootfs_console_ready())
		ir0_boot_smoke("ARM64_CONSOLE_OK");
	if (early_rootfs_memdev_ready())
		ir0_boot_smoke("ARM64_MEMDEV_OK");
	if (early_rootfs_dents_ready())
		ir0_boot_smoke("ARM64_DENTS_OK");
	if (early_rootfs_vda_ready())
		ir0_boot_smoke("ARM64_VDA_OK");
}

int arm64_rootfs_ready(void)
{
	return early_rootfs_ready();
}

int64_t arm64_rootfs_openat(int dirfd, uint64_t path, int flags)
{
	return early_rootfs_openat(dirfd, path, flags);
}

int64_t arm64_rootfs_read(int fd, uint64_t buf, uint64_t count)
{
	return early_rootfs_read(fd, buf, count);
}

int64_t arm64_rootfs_close(int fd)
{
	return early_rootfs_close(fd);
}

int64_t arm64_rootfs_readlinkat(int dirfd, uint64_t path, uint64_t buf,
				uint64_t bufsiz)
{
	return early_rootfs_readlinkat(dirfd, path, buf, bufsiz);
}

int64_t arm64_rootfs_faccessat(int dirfd, uint64_t path, int flags)
{
	return early_rootfs_faccessat(dirfd, path, flags);
}

int64_t arm64_rootfs_newfstatat(int dirfd, uint64_t path, uint64_t statbuf,
				int flags)
{
	return early_rootfs_newfstatat(dirfd, path, statbuf, flags);
}

int64_t arm64_rootfs_fstat(int fd, uint64_t statbuf)
{
	return early_rootfs_fstat(fd, statbuf);
}

int arm64_rootfs_smoke_init(void)
{
	return early_rootfs_smoke_init();
}
