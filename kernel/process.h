/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <ir0/task.h>
#include <ir0/signals.h>  
#include <ir0/types.h>

#ifndef ECHILD
#define ECHILD 10 /* No child processes (POSIX; matches ir0/errno.h) */
#endif

/* waitpid / wait4 options and status inspection (POSIX-style) */
#define WNOHANG 1

#define WEXITSTATUS(s) (((s) >> 8) & 0xFF)
#define WIFEXITED(s) (((s) & 0x7F) == 0)

#define MAX_FDS_PER_PROCESS 64

#ifndef IR0_NGROUPS_MAX
#define IR0_NGROUPS_MAX 32
#endif

struct robust_list_head;

typedef struct fd_entry
{
	bool in_use;
	char path[256];
	int flags;       /* Open flags (O_RDONLY, O_WRONLY, O_APPEND, etc.) */
	uint8_t fd_flags; /* FD_CLOEXEC etc. */
	void *vfs_file;
	uint64_t offset; /* File offset for seek operations */
	bool is_pipe;  /* 1 if this fd is a pipe */
	int pipe_end;  /* 0 = read end, 1 = write end */
	bool is_devfs; /* bound to devfs node when true */
	uint32_t dev_device_id;
	bool is_socket; /* bound to sock_udp when true */
} fd_entry_t;

/* Process execution mode */
typedef enum
{
	KERNEL_MODE = 0,  /* Running in kernel (dbgshell, embedded init) */
	USER_MODE = 1     /* Running in userspace (real processes) */
} process_mode_t;

typedef enum
{
	PROCESS_READY = 0,
	PROCESS_RUNNING,
	PROCESS_BLOCKED,
	PROCESS_ZOMBIE
} process_state_t;

/* Tracked anonymous/file mmap regions for demand paging and munmap */
struct mmap_region
{
	void *addr;
	void *hint_addr;
	size_t length;
	int prot;
	int flags;
	struct mmap_region *next;
};

/*
 * User register snapshot at syscall entry (Linux pt_regs subset).
 * Captured before dispatch runs nested handlers (fork, wait4, etc.).
 */
typedef struct
{
	uint64_t rip;
	uint64_t rflags;
	uint64_t rsp;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rdi;
	uint64_t rsi;
	uint64_t rdx;
	uint64_t r10;
	uint64_t r8;
	uint64_t r9;
} syscall_user_frame_t;

typedef struct process
{
	task_t task;
	/*
	 * x86-64 TLS base (MSR IA32_FS_BASE).
	 * Persisted across context switches because IR0 does not yet save/restore
	 * FS_BASE in switch_context_x64 / arch_switch_to_user_task_asm. Linux ABI
	 * (musl, glibc) sets this via arch_prctl(ARCH_SET_FS) on every task.
	 * The asm restore paths read this field directly via a hard-coded offset
	 * (guarded by _Static_assert in this file).
	 */
	uint64_t fs_base;
	pid_t ppid;
	uint64_t *page_directory;
	uint8_t owns_page_directory; /* 0 = shared kernel CR3 (idle task) */
	struct mmap_region *mmap_list;
	uint64_t mmap_base; /* top-down cursor for kernel-chosen mmap(NULL) */
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
	process_state_t state;
	process_mode_t mode;  /* Execution mode (kernel vs user) */
	int exit_code;
	int exit_signal; /* >0 if killed by signal (wait WIFSIGNALED / WTERMSIG) */
	struct process *next;
	fd_entry_t fd_table[MAX_FDS_PER_PROCESS];
	
	/* User and permissions */
	uint32_t uid;
	uint32_t gid;
	uint32_t euid;
	uint32_t egid;
	uint32_t umask;
	gid_t groups[IR0_NGROUPS_MAX];
	uint8_t ngroups;
	pid_t tgid;
	struct robust_list_head *robust_list;
	
	/* Current working directory */
	char cwd[256];
	
	/* Process command name (for ps) */
	char comm[16]; /* Process command name (max 15 chars + null) */
	
	/* Poll: waiter activo mientras el proceso está bloqueado en poll() */
	void *poll_waiter;
	uint8_t poll_resume_via_arch;
	uint8_t clock_wait_armed;
	uint8_t syscall_interrupted;
	uint64_t clock_wait_deadline_ms;

	/* Signal management */
	uint32_t signal_pending; /* Bitmask of pending signals */
	/* Signal handlers (function pointers to userspace handlers) */
	void (*signal_handlers[_NSIG])(int);  /* Array of signal handler functions */
	uint32_t signal_mask;  /* Mask of signals to block */
	uint32_t signal_ignored;  /* Mask of signals to ignore (SIG_IGN) */
	uint32_t signal_sa_flags[_NSIG]; /* Per-signal sa_flags from sigaction */
	uint32_t signal_sa_mask[_NSIG];  /* Per-signal sa_mask (during handler only) */
	int *set_tid_ptr;      /* set_tid_address(2) userspace pointer */
	struct sigcontext *saved_context;  /* Saved context before signal handler (for sigreturn) */

	/* Linux syscall insn frame (for fork child / blocked syscall return). */
	syscall_user_frame_t syscall_frame;
	uint64_t syscall_resume_rax;
	uint8_t irq_frame_saved; /* blocked syscall: resume via arch_switch_to_user_task */
	int *wait_status_ptr;    /* userspace wait4 status word while irq_frame_saved */
	/*
	 * wait4 blocked-syscall contract (D1.17): while irq_frame_saved from wait4,
	 * wait_target_pid holds the pid argument (>0 specific child; -1/0 any child).
	 * Wake/resume/reap paths must honour this — never complete wait4(pid>0) for
	 * another child, even if syscall_resume_rax was stale or mis-set.
	 */
	uint8_t wait_blocked;
	pid_t wait_target_pid;
	int wait_options;
	pid_t wait_resume_child_pid;

	/*
	 * Child blocked off runqueue until parent completes fork syscall return
	 * (process_fork_wake_pending at syscall exit; IRQs off until sysret).
	 */
	struct process *fork_pending_child;
	/*
	 * Parent: rewrite syscall_insn_entry stack slots from syscall_frame once
	 * after fork (global syscall stack can clobber saves during fork()).
	 */
	uint8_t fork_resync_syscall_stack;

	/*
	 * Cooperative in-syscall reschedule (syscall-insn / musl tasks).
	 * syscall_frame_fresh: this entry captured a valid Linux pt_regs in
	 *   syscall_frame (set only by process_capture_syscall_frame_at_entry,
	 *   i.e. the `syscall` insn path; int 0x80 tasks leave it 0).
	 * coop_resched_resume: the pending resume was armed by a cooperative
	 *   reschedule (not wait4), so the resume path must skip the zombie reap.
	 * Both let a time-sliced task resume via its syscall_frame (fresh iretq)
	 * instead of kernel_ret on the single shared global syscall stack, whose
	 * frame a peer task's syscall would clobber from the top.
	 */
	uint8_t syscall_frame_fresh;
	uint8_t coop_resched_resume;

	/*
	 * Per-process kernel stack (see IR0_PROC_KSTACK_SIZE). kstack_base is the
	 * kmalloc_aligned allocation (freed in process_destroy); kstack_top is the
	 * 16-byte aligned top loaded into kernel_syscall_stack_top and TSS.rsp0 when
	 * this task is scheduled. saved_user_rsp shadows the global user_rsp_save
	 * across context switches so a task resuming an in-kernel block loop restores
	 * its own user RSP at sysret instead of a peer's clobbered value.
	 */
	void *kstack_base;
	uint64_t kstack_top;
	uint64_t saved_user_rsp;
} process_t;

/*
 * ASM offset contract: switch_x64.asm restores FS_BASE by reading
 * [r11 + PROC_FS_BASE_OFFSET]. The asm hard-codes the numeric offset
 * for portability with NASM; this assert keeps both ends in sync.
 *
 * The placeholder forces a compile error showing the real value if it drifts.
 */
/*
 * PROC_FS_BASE_OFFSET (0x258) is hard-coded in switch_x64.asm to load
 * IA32_FS_BASE before returning to user. Keep both ends in sync.
 */
#if defined(__x86_64__) || defined(__amd64__)
_Static_assert(offsetof(process_t, task) == 0,        "process_t.task must be at offset 0");
_Static_assert(offsetof(process_t, fs_base) == 0x258, "switch_x64.asm PROC_FS_BASE_OFFSET out of sync");
#endif

static inline process_t *task_to_process(task_t *task)
{
	return (process_t *)((char *)task - offsetof(process_t, task));
}

static inline const process_t *task_to_process_const(const task_t *task)
{
	return (const process_t *)((const char *)task - offsetof(process_t, task));
}

/* PUBLIC MACROS - Register accessors                                        */

#define process_rax(p)    ((p)->task.rax)
#define process_rbx(p)    ((p)->task.rbx)
#define process_rcx(p)    ((p)->task.rcx)
#define process_rdx(p)    ((p)->task.rdx)
#define process_rsi(p)    ((p)->task.rsi)
#define process_rdi(p)    ((p)->task.rdi)
#define process_rsp(p)    ((p)->task.rsp)
#define process_rbp(p)    ((p)->task.rbp)
#define process_rip(p)    ((p)->task.rip)
#define process_rflags(p) ((p)->task.rflags)
#define process_cs(p)     ((p)->task.cs)
#define process_ss(p)     ((p)->task.ss)
#define process_ds(p)     ((p)->task.ds)
#define process_es(p)     ((p)->task.es)
#define process_fs(p)     ((p)->task.fs)
#define process_gs(p)     ((p)->task.gs)
#define process_pid(p)    ((p)->task.pid)


void process_capture_syscall_frame(process_t *p);
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw);
void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax);
void process_sync_task_user_ip_from_syscall_frame(process_t *p);

void process_restore_user_task_segments(process_t *p);

#if defined(__x86_64__) || defined(__amd64__)
void process_save_user_context_from_irq_frame(uint64_t *gpr_stack);
#endif

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax);
void process_arm_coop_resched_resume(process_t *p, uint64_t rax);
void process_clear_in_thread_syscall_block(process_t *p);
void process_reset_blocked_syscall_state(process_t *p);
void process_arm_kernel_syscall_sleep(process_t *p);
void fork_ret_emit_pre_return(void);
void fork_restore_emit_pre_iretq(void);
void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw);
int fork_flow_note_debug_exception(uint64_t *stack);
void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall);
__attribute__((noreturn)) void process_exit(int code);
int process_wait(pid_t pid, int *status, int options);

/*
 * True when parent is blocked in wait4 and child_pid satisfies the wait target.
 * Exported for ktests; used by wake/resume/reap paths.
 */
int process_wait_child_matches_blocked_target(const process_t *parent,
					      pid_t child_pid);

/* Linux wait status word for a zombie (exit << 8 or WTERMSIG). */
int process_child_wait_status_word(const process_t *child);

/*
 * Fatal default action for kill(2): SIGKILL always; SIGTERM when not caught or
 * ignored (signal mask does not defer default terminate). Returns 1 if @target
 * is now a zombie; caller must request schedule.
 */
int process_signal_default_kill(process_t *target, int signal);

/*
 * Reap a zombie child when resuming a blocked wait4 syscall.
 * Must run after the child has finished process_exit(), before returning to user.
 */
void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid);

/* IR0 PHILOSOPHY: Only spawn() creates processes - total simplicity
 * Mode must be explicitly specified - no magic address detection */
pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode);

/* Convenience wrappers for explicit mode specification */
pid_t spawn_user(void (*entry)(void), const char *name);
pid_t spawn_kernel(void (*entry)(void), const char *name);

/* Fork exists only for POSIX syscall compatibility - uses spawn() internally */
pid_t fork(void);

/*
 * Enqueue fork_pending_child after parent syscall retval is committed.
 * Called from syscall_dispatch on fork/clone/vfork exit only.
 */
void process_fork_wake_pending(process_t *parent);
void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot);


pid_t process_get_pid(void);
pid_t process_get_ppid(void);
process_t *process_get_current(void);
void irq_save_user_frame(uint64_t *frame);
process_t *get_process_list(void);
pid_t process_get_next_pid(void);
process_t *process_find_by_pid(pid_t pid);  /* Find process by PID */



uint64_t create_process_page_directory(void);
void process_init_fd_table(process_t *process);

/* Process lifecycle management */
void process_reap_zombies(process_t *parent); /* Reap zombie children (used by init) */
void process_reap_zombie_child(process_t *child);
void process_destroy(process_t *p);

/*
 * Per-process kernel stack lifecycle (IR0_PROC_KSTACK_SIZE). alloc returns
 * 0 on success or a negative errno; free is idempotent (NULL-safe).
 */
int process_kernel_stack_alloc(process_t *p);
void process_kernel_stack_free(process_t *p);
void process_unmap_user_address_space(process_t *p);
int process_remove_from_list(process_t *target);

uint64_t *process_pt_child(uint64_t *table, size_t index);
void process_fase50_trace_proc(const char *stage, process_t *p);

#include "debug/fase_audit.h"

int64_t process_close_fd(process_t *proc, int fd);
void process_exec_close_cloexec(process_t *p);
bool process_user_va_range_overlaps(process_t *proc, uintptr_t addr, size_t length);

uint64_t process_list_count(void);
uint64_t process_list_count_user(void);

extern process_t *current_process;
extern process_t *process_list;
