/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: early_process_internal.h
 * Description: shared state and helpers for early_process*.c (staged process
 *              path). Not a public ABI.
 */

#pragma once

#include <stdint.h>

#include <ir0/clone.h>
#include <ir0/early_process.h>
#include <ir0/early_rootfs.h>
#include <ir0/early_syscall.h>
#include <ir0/errno.h>
#include <ir0/fd_install.h>
#include <ir0/process.h>
#include <ir0/signals.h>
#include <ir0/time.h>
#include <ir0/utsname.h>

/* --- Local constants (not mid-file) --- */
#define EP_WNOHANG 1
#define EP_WEXITED 4
#define EP_P_ALL 0
#define EP_P_PID 1
#define EP_P_PGID 2
#define EP_CHILD_NONE 0
#define EP_CHILD_ENTER 1
#define EP_CHILD_RUNNING 2
#define EP_CHILD_ZOMBIE 3
#define EP_CHILD_PARKED 4
#define EP_EARLY_CHILD_PID 2
#define EP_LINUX_REBOOT_MAGIC1 0xfee1deadUL
#define EP_LINUX_REBOOT_MAGIC2 672274793UL
#define EP_LINUX_REBOOT_MAGIC2A 85072278UL
#define EP_LINUX_REBOOT_MAGIC2B 369367448UL
#define EP_LINUX_REBOOT_MAGIC2C 537993216UL
#define EP_UID_NOCHANGE 0xffffffffUL
#define EP_PRIO_PROCESS 0

#define EP_FUTEX_WAIT 0
#define EP_FUTEX_WAKE 1
#define EP_FUTEX_PRIVATE_FLAG 128
#define EP_FUTEX_CLOCK_REALTIME 256
#define EP_FUTEX_CMD_MASK ~(EP_FUTEX_PRIVATE_FLAG | EP_FUTEX_CLOCK_REALTIME)
#define EP_SS_DISABLE 2

/* --- File linkage state (early_process_state.c) --- */
extern int ep_g_getpid_ok;
extern int ep_g_umask;
extern uint32_t ep_g_uid;
extern uint32_t ep_g_euid;
extern uint32_t ep_g_suid;
extern uint32_t ep_g_fsuid;
extern uint32_t ep_g_gid;
extern uint32_t ep_g_egid;
extern uint32_t ep_g_sgid;
extern uint32_t ep_g_fsgid;
extern int32_t ep_g_nice;
extern int64_t ep_g_current_pid;
extern int64_t ep_g_child_pid;
extern int (*ep_g_common_task_exit)(int code);
extern void (*ep_g_schedule_exit)(int code);
extern int ep_g_schedule_exit_armed;
extern int ep_g_common_task_exit_published;
extern const struct ir0_syscall_identity_ops *ep_g_identity;
extern int ep_g_identity_used;
extern const struct ir0_fd_install_ops *ep_g_files;
extern int ep_g_child_state;
extern int ep_g_enter_child;
extern int ep_g_resume_parent;
extern uint64_t ep_g_child_stack;
extern uint64_t ep_g_child_tls;
extern uint64_t ep_g_child_cleartid;
extern int ep_g_child_wstatus;
extern int ep_g_vfork;
extern int ep_g_vfork_exec;
extern int ep_g_yield_requested;
extern int ep_g_block_wait_ok;
extern uint64_t ep_g_parent_frame[IR0_EARLY_EL0_FRAME_WORDS];
extern uint64_t ep_g_child_frame[IR0_EARLY_EL0_FRAME_WORDS];
extern int ep_g_child_frame_valid;
extern uint64_t ep_g_sigmask;
extern uint64_t ep_g_sa_handler[65];
extern uint64_t ep_g_sa_flags[65];
extern char ep_g_comm[IR0_EARLY_TASK_COMM_LEN];
extern int ep_g_pdeathsig;
extern int ep_g_dumpable;
extern int ep_g_keepcaps;
extern int ep_g_no_new_privs;
extern int ep_g_thp_disable;
extern uint64_t ep_g_clear_tid;
extern uint64_t ep_g_capbset;
extern int ep_g_sched_policy;
extern int32_t ep_g_sched_priority;
extern char ep_g_nodename[];
extern char ep_g_domainname[];
extern uint32_t ep_g_cap[6];
extern int ep_g_ioprio;
extern uint32_t ep_g_personality;
extern uint64_t ep_g_altstack_sp;
extern uint64_t ep_g_altstack_size;
extern int ep_g_altstack_flags;

void ep_early_set_current_pid(int64_t pid);
void ep_early_publish_creds(void);
int64_t ep_early_process_syscall(void *opaque, enum ir0_syscall_id id,
				 uint64_t a0, uint64_t a1, uint64_t a2,
				 uint64_t a3, uint64_t a4, uint64_t a5);
