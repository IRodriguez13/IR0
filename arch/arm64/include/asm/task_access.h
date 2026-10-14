/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#define IR0_TASK_MM_ROOT_FIELD ttbr0_el1
#define IR0_TASK_IP_FIELD elr_el1
#define IR0_TASK_SP_FIELD sp_el0
#define IR0_TASK_RETVAL_FIELD x0
#define IR0_TASK_FLAGS_FIELD spsr_el1
#define IR0_TASK_FP_FIELD x29
#define IR0_TASK_ARG0_FIELD x0
#define IR0_TASK_ARG1_FIELD x1
#define IR0_TASK_ARG2_FIELD x2
#define IR0_TASK_GET_CS(t) ((uint16_t)0)
#define IR0_TASK_SET_CS(t,v) do { (void)(t); (void)(v); } while (0)
#define IR0_TASK_GET_SS(t) ((uint16_t)0)
#define IR0_TASK_SET_SS(t,v) do { (void)(t); (void)(v); } while (0)
#define IR0_TASK_SET_USER_STATE(t) do { (void)(t); } while (0)
#define IR0_TASK_SET_KERNEL_STATE(t) do { (void)(t); } while (0)
#define IR0_TASK_RETURN_STATE_IS_USER(t) ((t) && (((t)->arch.spsr_el1 & 0xfu) == 0u))
