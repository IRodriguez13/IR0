/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: test_bpivot_boot.c
 * Description: Ktest for portable bpivot product boot on common VFS.
 */

#include "test/ktest_harness.h"
#include "test/ktest_decl.h"

#include <ir0/bpath.h>
#include <ir0/bpivot.h>
#include <ir0/open_flags.h>
#include <ir0/ramtab.h>
#include <ir0/stat.h>
#include <ir0/vfs.h>
#include <stdint.h>
#include <string.h>

static void bpivot_hex8(uint8_t *dst, uint32_t value)
{
	static const char digits[] = "0123456789abcdef";
	int i;

	for (i = 7; i >= 0; i--)
	{
		dst[i] = (uint8_t)digits[value & 0xfU];
		value >>= 4;
	}
}

static size_t bpivot_cpio_member(uint8_t *buf, size_t cap, size_t off,
				const char *name, uint32_t mode,
				const uint8_t *data, uint32_t size)
{
	size_t namesize = strlen(name) + 1;
	size_t data_off;
	size_t next;

	if (off + 110 + namesize > cap)
		return 0;
	memset(buf + off, '0', 110);
	memcpy(buf + off, "070701", 6);
	bpivot_hex8(buf + off + 14, mode);
	bpivot_hex8(buf + off + 54, size);
	bpivot_hex8(buf + off + 94, (uint32_t)namesize);
	memcpy(buf + off + 110, name, namesize);
	data_off = (off + 110 + namesize + 3U) & ~3U;
	next = (data_off + size + 3U) & ~3U;
	if (next > cap)
		return 0;
	memset(buf + off + 110 + namesize, 0, next - (off + 110 + namesize));
	if (size > 0)
		memcpy(buf + data_off, data, size);
	return next;
}

static int bpivot_copy_from_user(void *destination, uint64_t source,
				 uint64_t size)
{
	memcpy(destination, (const void *)(uintptr_t)source, (size_t)size);
	return 0;
}

static int bpivot_copy_to_user(uint64_t destination, const void *source,
			       uint64_t size)
{
	memcpy((void *)(uintptr_t)destination, source, (size_t)size);
	return 0;
}

void ktest_bpivot_boot_contract(void)
{
	static uint8_t cpio[2048];
	static uint8_t init_data[64];
	static const uint8_t probe_data[] = "pivot";
	static const struct early_user_services user = {
		.copy_from_user = bpivot_copy_from_user,
		.copy_to_user = bpivot_copy_to_user,
	};
	const struct ir0_bpath_ops *ops;
	stat_t st;
	size_t off;
	int ret;

	KTEST_BEGIN("bpivot_boot_contract");

	ops = ir0_bpath_vfs_product_ops();
	KASSERT(ops != NULL);
	memset(init_data, 0, sizeof(init_data));
	init_data[0] = 0x7f;
	init_data[1] = 'E';
	init_data[2] = 'L';
	init_data[3] = 'F';
	init_data[4] = 2;
	init_data[5] = 1;
	memset(cpio, 0, sizeof(cpio));
	off = bpivot_cpio_member(cpio, sizeof(cpio), 0, "init",
				 S_IFREG | 0755, init_data, sizeof(init_data));
	KASSERT(off > 0U);
	off = bpivot_cpio_member(cpio, sizeof(cpio), off, "etc",
				 S_IFDIR | 0755, NULL, 0);
	KASSERT(off > 0U);
	off = bpivot_cpio_member(cpio, sizeof(cpio), off, "etc/pivot-probe",
				 S_IFREG | 0644, probe_data, 5);
	KASSERT(off > 0U);
	off = bpivot_cpio_member(cpio, sizeof(cpio), off, "TRAILER!!!",
				 0, NULL, 0);
	KASSERT(off > 0U);
	KASSERT_EQ(early_rootfs_bind_cpio(cpio, off, &user), 0);
	KASSERT_GE(ramtab_find("/etc/pivot-probe"), 0);

	ret = ir0_bpivot_product_boot(ops);
	KASSERT_EQ(ret, 0);
	KASSERT(ir0_bpivot_committed());
	KASSERT(ir0_bpivot_root_switched());
	KASSERT(ir0_bpivot_cpio_fold_done());
	KASSERT_EQ(ramtab_find("/etc/pivot-probe"), -1);
	KASSERT_EQ(vfs_stat("/etc/pivot-probe", &st), 0);
	KASSERT(S_ISREG(st.st_mode));
	KASSERT_EQ(vfs_stat("/old", &st), 0);
	KASSERT(S_ISDIR(st.st_mode));

	KTEST_END();
}
