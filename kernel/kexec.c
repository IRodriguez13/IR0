/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kexec.c
 * Description: kexec_load MVP — segment copy into kernel RAM; reboot path.
 *
 * References:
 * - Linux man7 kexec_load(2)
 * - Linux kernel/kexec.c (segment cap, arch flags)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/kexec.h>
#include <ir0/copy_user.h>
#include <ir0/errno.h>
#include <ir0/kmem.h>
#include <ir0/serial_io.h>
#include <ir0/credentials.h>
#include <stddef.h>
#include <stdint.h>

#define IR0_KEXEC_MAGIC "IR0KEXEC"
#define IR0_KEXEC_MAGIC_LEN 8
#define IR0_KEXEC_MAX_BYTES (256u * 1024u)

static void *g_image;
static size_t g_image_len;
static unsigned long g_entry;
static int g_loaded;

static void kexec_clear(void)
{
	if (g_image)
	{
		kfree(g_image);
		g_image = NULL;
	}
	g_image_len = 0;
	g_entry = 0;
	g_loaded = 0;
}

int ir0_kexec_image_loaded(void)
{
	return g_loaded;
}

int64_t sys_kexec_load(unsigned long entry, unsigned long nr_segments,
		       struct kexec_segment *segments, unsigned long flags)
{
	struct kexec_segment ksegs[KEXEC_SEGMENT_MAX];
	size_t total;
	size_t off;
	unsigned long i;
	unsigned long arch;
	uint8_t *dst;

	if (!ir0_cred_is_root())
		return -EPERM;

	if (flags & KEXEC_ON_CRASH)
		return -EINVAL;

	arch = flags & KEXEC_ARCH_MASK;
	if (arch != KEXEC_ARCH_DEFAULT && arch != KEXEC_ARCH_X86_64)
		return -EINVAL;

	if (nr_segments == 0 || nr_segments > KEXEC_SEGMENT_MAX)
		return -EINVAL;
	if (!segments)
		return -EFAULT;

	if (copy_from_user(ksegs, segments,
			   nr_segments * sizeof(ksegs[0])) != 0)
		return -EFAULT;

	total = 0;
	for (i = 0; i < nr_segments; i++)
	{
		if (ksegs[i].bufsz > ksegs[i].memsz)
			return -EINVAL;
		if (ksegs[i].memsz == 0 || !ksegs[i].buf)
			return -EINVAL;
		if (ksegs[i].memsz > IR0_KEXEC_MAX_BYTES)
			return -ENOMEM;
		if (total + ksegs[i].memsz < total)
			return -EINVAL;
		total += ksegs[i].memsz;
		if (total > IR0_KEXEC_MAX_BYTES)
			return -ENOMEM;
	}

	kexec_clear();
	dst = (uint8_t *)kmalloc_try(total);
	if (!dst)
		return -ENOMEM;

	off = 0;
	for (i = 0; i < nr_segments; i++)
	{
		size_t copy_n = ksegs[i].bufsz;
		size_t pad = ksegs[i].memsz - copy_n;

		if (copy_n > 0 &&
		    copy_from_user(dst + off, ksegs[i].buf, copy_n) != 0)
		{
			kfree(dst);
			return -EFAULT;
		}
		if (pad > 0)
		{
			size_t z;

			for (z = 0; z < pad; z++)
				dst[off + copy_n + z] = 0;
		}
		off += ksegs[i].memsz;
	}

	g_image = dst;
	g_image_len = total;
	/* entry 0 → start of kernel-held image (smoke / default). */
	g_entry = entry ? entry : (unsigned long)(uintptr_t)dst;
	g_loaded = 1;
	serial_print("KEXEC_LOAD_OK\n");
	return 0;
}

void ir0_kexec_execute(void)
{
	void (*fn)(void);

	if (!g_loaded || !g_image)
	{
		serial_print("REBOOT_KEXEC_STUB\n");
		return;
	}

	serial_print("REBOOT_KEXEC_LOADED\n");

	if (g_image_len >= IR0_KEXEC_MAGIC_LEN)
	{
		const char *m = IR0_KEXEC_MAGIC;
		const uint8_t *p = (const uint8_t *)g_image;
		int match = 1;
		size_t i;

		for (i = 0; i < IR0_KEXEC_MAGIC_LEN; i++)
		{
			if (p[i] != (uint8_t)m[i])
			{
				match = 0;
				break;
			}
		}
		if (match)
		{
			serial_print("KEXEC_PAYLOAD_OK\n");
			/* Magic smoke payload: path proven; soft reboot next. */
			return;
		}
	}

	fn = (void (*)(void))(uintptr_t)g_entry;
	fn();
	for (;;)
		__asm__ __volatile__("hlt");
}
