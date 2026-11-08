/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: oops.h
 * Description: IR0 kernel source/header file
 */

#pragma once

#include <stdint.h>

/* Semantic panic levels */
typedef enum
{
    PANIC_KERNEL_BUG = 0,      /* Bug in kernel code */
    PANIC_HARDWARE_FAULT = 1,  /* Hardware malfunction */
    PANIC_OUT_OF_MEMORY = 2,   /* System out of memory */
    PANIC_STACK_OVERFLOW = 3,  /* Stack corruption */
    PANIC_ASSERT_FAILED = 4,   /* Assertion failure */
    PANIC_MEM = 5,             /* Memory operation error (null ptr, invalid access, etc.) */
    TESTING = 6,
    RUNNING_OUT_PROCESS = 7,
    /* Test only (/sys/kernel/panic "nested"): fault inside panicex. */
    PANIC_TEST_NESTED_FAULT = 8
} panic_level_t;

#ifdef __cplusplus
extern "C" {
#endif

void panicex(const char *message, panic_level_t level, const char *file, int line, const char *caller); /*Panic with more detailed logs*/
struct ir0_fault;

/*
 * Record the CPU fault before panicex so the banner shows the fault site
 * (ip/sp) instead of only the panic reporter (__func__). The first fault
 * wins: a nested fault during the dump is only named, never recorded.
 */
void panic_note_fault(const struct ir0_fault *fault, unsigned int pid,
		      const char *comm);
unsigned long long ir0_panic_fault_rsp(void);

/* 1 when @sp sits in the unmapped guard page of a kernel stack slot. */
int ir0_fault_sp_in_kstack_guard(uintptr_t sp);

/*
 * Greppable userspace fault line for session smokes (USER_FAULT_FRAME).
 * Safe during IRQ; does not panic. Emitted before the signal is delivered
 * or the task is killed.
 */
void ir0_log_user_fault(const struct ir0_fault *fault, unsigned int pid,
			const char *comm);
void dump_stack_trace(void);
void dump_registers(void);

#ifdef __cplusplus
}
#endif

/* Call-site metadata for KTM panic inventory (scripts/ktm_panic_inventory.py). */
#define panic(message) \
	panicex((message), PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__)

#define PANIC(msg) \
    panicex((msg), PANIC_KERNEL_BUG, __FILE__, __LINE__, __func__)

/* For testing */
#define ASSERT(condition) \
    do { \
        if (unlikely(!(condition))) \
        { \
            panicex("ASSERT failed: " #condition, PANIC_ASSERT_FAILED, __FILE__, __LINE__, __func__); \
        } \
    } while(0)
      
#define likely(x)   __builtin_expect(!!(x), 1)
#define unlikely(x) __builtin_expect(!!(x), 0)

