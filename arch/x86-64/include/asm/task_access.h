/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once
#define IR0_TASK_MM_ROOT_FIELD cr3
#define IR0_TASK_IP_FIELD rip
#define IR0_TASK_SP_FIELD rsp
#define IR0_TASK_RETVAL_FIELD rax
#define IR0_TASK_FLAGS_FIELD rflags
#define IR0_TASK_FP_FIELD rbp
#define IR0_TASK_ARG0_FIELD rdi
#define IR0_TASK_ARG1_FIELD rsi
#define IR0_TASK_ARG2_FIELD rdx
#define IR0_TASK_GET_CS(t) ((t) ? (t)->arch.cs : 0)
#define IR0_TASK_SET_CS(t,v) do { if (t) (t)->arch.cs = (v); } while (0)
#define IR0_TASK_GET_SS(t) ((t) ? (t)->arch.ss : 0)
#define IR0_TASK_SET_SS(t,v) do { if (t) (t)->arch.ss = (v); } while (0)
#define IR0_TASK_SET_USER_STATE(t) do { (t)->arch.cs=0x1b; (t)->arch.ss=0x23; (t)->arch.ds=0x23; (t)->arch.es=0x23; (t)->arch.fs=0x23; (t)->arch.gs=0x23; } while (0)
#define IR0_TASK_SET_KERNEL_STATE(t) do { (t)->arch.cs=0x08; (t)->arch.ss=0x10; (t)->arch.ds=0x10; (t)->arch.es=0x10; (t)->arch.fs=0x10; (t)->arch.gs=0x10; } while (0)
#define IR0_TASK_RETURN_STATE_IS_USER(t) ((t) && (((t)->arch.cs & 3u) != 0u))
