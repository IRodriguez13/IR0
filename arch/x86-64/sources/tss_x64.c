/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: tss_x64.c
 * Description: IR0 kernel source/header file
 */

#include <stdint.h>
#include <ir0/vga.h>
#include <stddef.h>
#include <string.h>
#include "gdt.h"
#include "tss_x64.h"


/* Global TSS instance */
tss_t kernel_tss __attribute__((aligned(4096)));

/* Kernel stack for Ring 0 - 32KB for safety */
static uint8_t kernel_stack[32768] __attribute__((aligned(4096)));

/*
 * Dedicated stack for double fault (#DF) via IST1. Prevents recursive #DF
 * when the primary #DF handler runs with a corrupt or exhausted RSP0 stack.
 */
static uint8_t df_ist_stack[8192] __attribute__((aligned(16)));

void setup_tss()
{
    /* Clear TSS structure */
    memset(&kernel_tss, 0, sizeof(tss_t));

    /* Set up kernel stack pointer for Ring 0 */
    kernel_tss.rsp0 = (uint64_t)(kernel_stack + sizeof(kernel_stack) - 16);

    /*
     * IST1: top of stack (16-byte down from end), index 1 in the IDT IST field.
     */
    kernel_tss.ist1 =
        (uint64_t)(df_ist_stack + sizeof(df_ist_stack) - 16);
    kernel_tss.ist2 = 0;
    kernel_tss.ist3 = 0;
    kernel_tss.ist4 = 0;
    kernel_tss.ist5 = 0;
    kernel_tss.ist6 = 0;
    kernel_tss.ist7 = 0;

    /* I/O permission bitmap */
    kernel_tss.iopb_offset = sizeof(tss_t);

    /* Update GDT TSS descriptor */
    update_gdt_tss((uint64_t)&kernel_tss);
}

