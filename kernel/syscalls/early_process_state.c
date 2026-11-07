/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: kernel/syscalls/early_process_state.c
 * Description: file-scope state for staged early process / clone helpers.
 */

#include "early_process_internal.h"

int ep_g_getpid_ok;
int ep_g_umask = 0022;
uint32_t ep_g_uid;
uint32_t ep_g_euid;
uint32_t ep_g_suid;
uint32_t ep_g_fsuid;
uint32_t ep_g_gid;
uint32_t ep_g_egid;
uint32_t ep_g_sgid;
uint32_t ep_g_fsgid;
int32_t ep_g_nice;
int64_t ep_g_current_pid = 1;
int64_t ep_g_child_pid = EP_EARLY_CHILD_PID;
int (*ep_g_common_task_exit)(int code);
void (*ep_g_schedule_exit)(int code);
int ep_g_schedule_exit_armed;
int ep_g_common_task_exit_published;
const struct ir0_syscall_identity_ops *ep_g_identity;
int ep_g_identity_used;
const struct ir0_fd_install_ops *ep_g_files;
int ep_g_child_state;
int ep_g_enter_child;
int ep_g_resume_parent;
uint64_t ep_g_child_stack;
uint64_t ep_g_child_tls;
uint64_t ep_g_child_cleartid;
int ep_g_child_wstatus;
int ep_g_vfork;
int ep_g_vfork_exec;
int ep_g_yield_requested;
int ep_g_block_wait_ok;
uint64_t ep_g_parent_frame[IR0_EARLY_EL0_FRAME_WORDS];
uint64_t ep_g_child_frame[IR0_EARLY_EL0_FRAME_WORDS];
int ep_g_child_frame_valid;
uint64_t ep_g_sigmask;
uint64_t ep_g_sa_handler[65];
uint64_t ep_g_sa_flags[65];
char ep_g_comm[IR0_EARLY_TASK_COMM_LEN] = { 'i', 'n', 'i', 't', 0 };
int ep_g_pdeathsig;
int ep_g_dumpable = 1;
int ep_g_keepcaps;
int ep_g_no_new_privs;
int ep_g_thp_disable;
uint64_t ep_g_clear_tid;
uint64_t ep_g_capbset = ~0ULL;
int ep_g_sched_policy;
int32_t ep_g_sched_priority;
char ep_g_nodename[IR0_EARLY_HOSTNAME_MAX + 1] = { 'i', 'r', '0', 0 };
char ep_g_domainname[IR0_EARLY_HOSTNAME_MAX + 1];
uint32_t ep_g_cap[6] = { 0xffffffffU, 0xffffffffU, 0, 0xffffffffU,
			 0xffffffffU, 0 };
int ep_g_ioprio;
uint32_t ep_g_personality;
uint64_t ep_g_altstack_sp;
uint64_t ep_g_altstack_size;
int ep_g_altstack_flags = EP_SS_DISABLE;
