/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: freestanding_stubs.c
 * Description: Minimal freestanding helpers for ARM64 kernel link (no libc).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stddef.h>
#include <stdint.h>

void arch_disable_interrupts(void)
{
	__asm__ volatile("msr daifset, #0xf" ::: "memory");
}

void arch_enable_interrupts(void)
{
	__asm__ volatile("msr daifclr, #0xf" ::: "memory");
}

void cpu_wait(void)
{
	__asm__ volatile("wfi" ::: "memory");
}

char *strncpy(char *dst, const char *src, size_t n)
{
	size_t i;

	if (!dst || !src)
		return dst;
	for (i = 0; i < n && src[i]; i++)
		dst[i] = src[i];
	for (; i < n; i++)
		dst[i] = '\0';
	return dst;
}
