/** IR0 Kernel — ISA-neutral task context accessors. */
/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#include <stdint.h>
#include <asm/task_access.h>
typedef struct task task_t;
static inline void task_context_init(arch_task_context_t *c) { if (c) *c = (arch_task_context_t){0}; }
static inline void task_context_clone(arch_task_context_t *d, const arch_task_context_t *s) { if (d && s) *d = *s; }
#define IR0_TASK_ACCESSOR(n, type, field) \
 static inline type task_get_##n(const task_t *t) { return t ? (type)t->arch.field : (type)0; } \
 static inline void task_set_##n(task_t *t, type v) { if (t) t->arch.field = v; }
IR0_TASK_ACCESSOR(ip, uint64_t, IR0_TASK_IP_FIELD)
IR0_TASK_ACCESSOR(sp, uint64_t, IR0_TASK_SP_FIELD)
IR0_TASK_ACCESSOR(retval, uint64_t, IR0_TASK_RETVAL_FIELD)
IR0_TASK_ACCESSOR(flags, uint64_t, IR0_TASK_FLAGS_FIELD)
IR0_TASK_ACCESSOR(arg0, uint64_t, IR0_TASK_ARG0_FIELD)
IR0_TASK_ACCESSOR(arg1, uint64_t, IR0_TASK_ARG1_FIELD)
IR0_TASK_ACCESSOR(arg2, uint64_t, IR0_TASK_ARG2_FIELD)
#undef IR0_TASK_ACCESSOR
static inline uint64_t task_mm_root(const task_t *t) { return t ? (uint64_t)t->arch.IR0_TASK_MM_ROOT_FIELD : 0; }
static inline void task_set_mm_root(task_t *t, uint64_t v) { if (t) t->arch.IR0_TASK_MM_ROOT_FIELD = v; }
static inline uint16_t task_get_cs(const task_t *t) { return IR0_TASK_GET_CS(t); }
static inline void task_set_cs(task_t *t, uint16_t v) { IR0_TASK_SET_CS(t, v); }
static inline uint16_t task_get_ss(const task_t *t) { return IR0_TASK_GET_SS(t); }
static inline void task_set_ss(task_t *t, uint16_t v) { IR0_TASK_SET_SS(t, v); }
static inline void task_set_user_return(task_t *t, uint64_t ip, uint64_t sp, uint64_t flags) { if (t) { task_set_ip(t, ip); task_set_sp(t, sp); task_set_flags(t, flags); IR0_TASK_SET_USER_STATE(t); } }
static inline void task_prepare_fork_child(arch_task_context_t *c, const arch_task_context_t *p) { if (c && p) { task_context_clone(c, p); c->IR0_TASK_RETVAL_FIELD = 0; } }
static inline void task_clear_frame_pointer(task_t *t) { if (t) t->arch.IR0_TASK_FP_FIELD = 0; }
static inline void task_set_frame_pointer(task_t *t, uint64_t v) { if (t) t->arch.IR0_TASK_FP_FIELD = v; }
static inline void task_set_user_segments(task_t *t) { if (t) IR0_TASK_SET_USER_STATE(t); }
static inline int task_cs_is_user(const task_t *t) { return IR0_TASK_RETURN_STATE_IS_USER(t); }
static inline int task_return_state_is_user(const task_t *t) { return IR0_TASK_RETURN_STATE_IS_USER(t); }
static inline void task_set_kernel_segments(task_t *t) { if (t) IR0_TASK_SET_KERNEL_STATE(t); }
