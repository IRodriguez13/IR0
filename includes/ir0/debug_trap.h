/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Trap Flag / #DB hygiene — gated single-step and safe debug handling.
 */

#pragma once

#include <stdint.h>
#include <config.h>

#define IR0_RFLAGS_TF 0x100ULL

#ifndef IR0_ENABLE_SINGLE_STEP
#define IR0_ENABLE_SINGLE_STEP 0
#endif

#ifndef IR0_ENABLE_FORK_SINGLESTEP_TRACE
#define IR0_ENABLE_FORK_SINGLESTEP_TRACE 0
#endif

void ir0_debug_trap_init(void);
int ir0_debug_single_step_active(void);
int ir0_debug_fork_singlestep_active(void);

uint64_t ir0_rflags_sanitize_user(uint64_t rflags);
uint64_t ir0_rflags_clear_tf(uint64_t rflags);

void ir0_debug_clear_dr6_dr7(void);
void ir0_debug_read_dr6_dr7(uint64_t *dr6, uint64_t *dr7);

/*
 * Handle #DB in user mode: clear stray TF/DR state unless single-step is
 * explicitly enabled.  Returns 1 if the fault was consumed (resume user).
 */
int ir0_debug_handle_user_db(uint64_t *stack);

/* Kernel #DB — panic with RIP/RFLAGS/DR6/DR7/current pid (does not return). */
void ir0_debug_handle_kernel_db(uint64_t *stack);
