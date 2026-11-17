/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: linux_abi_kexec.c
 * Description: Selftest: kexec_load(2) architecture check. An image built
 *              for another architecture is refused with EINVAL; the native
 *              and the default architecture are accepted. Each case passes
 *              on Linux and on IR0. Run by
 *              make ir0-selftest-kexec-abi-{x86,arm64}.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "conf.h"

#include <sys/mman.h>

/* include/uapi/linux/kexec.h; musl ships no <linux/kexec.h>. */
#define LINUX_KEXEC_ARCH_DEFAULT (0UL << 16)
#define LINUX_KEXEC_ARCH_X86_64 (62UL << 16)
#define LINUX_KEXEC_ARCH_AARCH64 (183UL << 16)

#if defined(__x86_64__)
#define KEXEC_ARCH_NATIVE LINUX_KEXEC_ARCH_X86_64
#define KEXEC_ARCH_FOREIGN LINUX_KEXEC_ARCH_AARCH64
#elif defined(__aarch64__)
#define KEXEC_ARCH_NATIVE LINUX_KEXEC_ARCH_AARCH64
#define KEXEC_ARCH_FOREIGN LINUX_KEXEC_ARCH_X86_64
#else
#error "linux_abi_kexec: unsupported ISA"
#endif

/* An address inside the user window with nothing mapped at it. */
static void *unmapped_page(void)
{
	long a = S6(SYS_mmap, 0, RT_PAGE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (a < 0 && a > -4096)
		return 0;
	if (S2(SYS_munmap, a, RT_PAGE) != 0)
		return 0;
	return (void *)a;
}

/*
 * kexec_load() on an unmapped segment list: the result tells which side of
 * the architecture check the call reached, and no image is ever staged.
 * -EPERM means not root or kexec locked down (a Linux host run): TCONF.
 */
static long kexec_load_arch(unsigned long arch)
{
	void *hole = unmapped_page();

	if (!hole)
		return 1;
	return S4(SYS_kexec_load, 0, 1, hole, arch);
}

/*
 * kernel/kexec.c SYSCALL_DEFINE4(kexec_load): KEXEC_ARCH_DEFAULT and the
 * native KEXEC_ARCH pass the architecture check and go on to copy the
 * segment list, which faults here.
 */
int kexec_load_native_arch_accepted(void)
{
	long r = kexec_load_arch(LINUX_KEXEC_ARCH_DEFAULT);

	if (r == -EPERM)
		return CONF_TCONF;
	EXPECT(r, -EFAULT);
	EXPECT(kexec_load_arch(KEXEC_ARCH_NATIVE), -EFAULT);
	return CONF_PASS;
}

/* Any other architecture is EINVAL before the segment list is read. */
int kexec_load_foreign_arch_einval(void)
{
	long r = kexec_load_arch(KEXEC_ARCH_FOREIGN);

	if (r == -EPERM)
		return CONF_TCONF;
	EXPECT(r, -EINVAL);
	return CONF_PASS;
}
