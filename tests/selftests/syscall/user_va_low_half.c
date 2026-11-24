/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: user_va_low_half.c
 * Description: arm64 TTBR0 holds only user mappings (Linux arm64): a
 *              process can MAP_FIXED where QEMU virt RAM and the old
 *              identity-mapped kernel image were, and reading such an
 *              address while unmapped is SIGSEGV, not kernel memory.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#ifdef IR0_SELFTEST_NOLIBC
#include "ir0/nolibc.h"
#else
#include <signal.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

#include "ir0/ksft.h"

/* QEMU virt DRAM base: the kernel image and heap used to sit here. */
#define FORMER_KERNEL_RAM   0x40000000UL
/* Former identity address of the kernel image (_start). */
#define FORMER_KERNEL_IMAGE 0x40080000UL

static void on_segv(int sig, siginfo_t *info, void *uc)
{
	(void)uc;
	ksft_test_result(sig == SIGSEGV && info &&
			 (unsigned long)info->si_addr == FORMER_KERNEL_IMAGE,
			 "former_kernel_image_read_segv");
	_exit(ksft_exit());
}

int main(void)
{
	struct sigaction sa;
	volatile unsigned char *p;
	volatile unsigned char v;

	ksft_set_plan(2);
	p = mmap((void *)FORMER_KERNEL_RAM, 4096, PROT_READ | PROT_WRITE,
		 MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
	if (p == (volatile unsigned char *)FORMER_KERNEL_RAM)
	{
		p[0] = 0x5a;
		p[4095] = 0xa5;
	}
	ksft_test_result(p == (volatile unsigned char *)FORMER_KERNEL_RAM &&
			 p[0] == 0x5a && p[4095] == 0xa5,
			 "map_fixed_former_kernel_ram");
	if (p != MAP_FAILED)
		(void)munmap((void *)p, 4096);

#ifdef IR0_SELFTEST_NOLIBC
	sa.sa_handler = (void (*)(int))on_segv;
	sa.sa_flags = SA_SIGINFO;
	sa.sa_restorer = 0;
	sa.sa_mask = 0;
#else
	memset(&sa, 0, sizeof(sa));
	sa.sa_sigaction = on_segv;
	sa.sa_flags = SA_SIGINFO;
#endif
	if (sigaction(SIGSEGV, &sa, 0) != 0)
	{
		ksft_test_result(0, "former_kernel_image_read_segv");
		return ksft_exit();
	}
	v = *(volatile unsigned char *)FORMER_KERNEL_IMAGE;
	(void)v;
	ksft_test_result(0, "former_kernel_image_read_segv");
	return ksft_exit();
}
