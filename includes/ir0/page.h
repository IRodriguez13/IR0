/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: page.h
 * Description: Direct-map facade (Linux asm/page.h phys_to_virt and
 *              virt_to_phys). Every physical address the kernel touches
 *              goes through it.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <asm/page.h>

/*
 * All of RAM is mapped once, at PAGE_OFFSET, in the kernel half shared by
 * every address space. Where the kernel image and its heap live is the
 * ISA's business: <asm/page.h> supplies __virt_to_phys() for them.
 */
#define PAGE_OFFSET IR0_ARCH_PAGE_OFFSET

/* Kernel pointer to physical address @phys (never 0: test before calling). */
static inline void *phys_to_virt(uintptr_t phys)
{
	return (void *)(phys + PAGE_OFFSET);
}

/*
 * Physical address behind a direct-map pointer or a kernel image or heap
 * pointer. Not for kernel stacks or other kernel-half mappings outside the
 * direct map.
 */
static inline uintptr_t virt_to_phys(const volatile void *addr)
{
	return __virt_to_phys((uintptr_t)addr);
}
