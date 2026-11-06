/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: x86_oops_port.c
 * Description: x86_64 extra panic diagnostics (iretq checkpoint buffer).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/oops_port.h>
#include <ir0/ktm/klog.h>

#include <stddef.h>
#include <stdint.h>

extern uint64_t iretq_checkpoint_buf[40];

void ir0_oops_dump_arch_extra(void)
{
	size_t k;

	klog_print("\n--- IRETQ CHECKPOINT BUFFER ---\n");
	for (k = 0; k < 40; k++)
	{
		klog_print("ckpt[");
		klog_hex32((uint32_t)k);
		klog_print("] = ");
		klog_hex64(iretq_checkpoint_buf[k]);
		klog_print("\n");
	}
}
