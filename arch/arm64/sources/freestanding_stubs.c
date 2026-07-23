/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: freestanding_stubs.c
 * Description: Minimal freestanding helpers for ARM64 kernel link (no libc).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <arch/common/arch_portable.h>

#include <stddef.h>
#include <stdint.h>

void disable_interrupts(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	daif |= (1UL << 7);
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

void enable_interrupts(void)
{
	unsigned long daif;

	__asm__ volatile("mrs %0, daif" : "=r"(daif));
	daif &= ~(1UL << 7);
	__asm__ volatile("msr daif, %0" :: "r"(daif) : "memory");
}

void cpu_wait(void)
{
	__asm__ volatile("wfi" ::: "memory");
}

void panic(const char *msg)
{
	(void)msg;
	for (;;)
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
