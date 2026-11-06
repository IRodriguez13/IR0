/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: oops_port.c
 * Description: Default weak ISA extra panic dump — strong x86 provides iretq buffer.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops_port.h>

__attribute__((weak)) void ir0_oops_dump_arch_extra(void)
{
}
