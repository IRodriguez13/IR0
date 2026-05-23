/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.h
 * Description: IR0 kernel source/header file
 */

/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Process Management
 * Copyright (C) 2025 Iván Rodriguez
 *
 * Public interface for process management subsystem
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <sched/task.h>
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

typedef enum
{
	FASE44_PROC_ALIVE = 0,
	FASE44_PROC_EXITING,
	FASE44_PROC_ZOMBIE,
	FASE44_PROC_REAPED,
	FASE44_PROC_DESTROYED
} fase44_audit_state_t;

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
	uint64_t heap_start;
	uint64_t heap_end;
	uint64_t stack_start;
	uint64_t stack_size;
	process_state_t state;
	process_mode_t mode;  /* Execution mode (kernel vs user) */
	int exit_code;
	struct process *next;
	fd_entry_t fd_table[MAX_FDS_PER_PROCESS];
	
	/* User and permissions */
	uint32_t uid;
	uint32_t gid;
	uint32_t euid;
	uint32_t egid;
	uint32_t umask;
	
	/* Current working directory */
	char cwd[256];
	
	/* Process command name (for ps) */
	char comm[16]; /* Process command name (max 15 chars + null) */
	
	/* Poll: waiter activo mientras el proceso está bloqueado en poll() */
	void *poll_waiter;

	/* Signal management */
	uint32_t signal_pending; /* Bitmask of pending signals */
	/* Signal handlers (function pointers to userspace handlers) */
	void (*signal_handlers[_NSIG])(int);  /* Array of signal handler functions */
	uint32_t signal_mask;  /* Mask of signals to block */
	uint32_t signal_ignored;  /* Mask of signals to ignore (SIG_IGN) */
	int *set_tid_ptr;      /* set_tid_address(2) userspace pointer */
	struct sigcontext *saved_context;  /* Saved context before signal handler (for sigreturn) */

	/* Linux syscall insn frame (for fork child / blocked syscall return). */
	syscall_user_frame_t syscall_frame;
	uint64_t syscall_resume_rax;
	uint8_t irq_frame_saved; /* blocked syscall: resume via arch_switch_to_user_task */

	/* FASE44 lifecycle audit (diagnostic only). */
	uint8_t fase44_audit_state;

	/* FASE46 per-process convergence audit. */
	uint32_t fase46_fork_generation;
	pid_t fase46_fork_parent_pid;
	uint8_t fase46_entered_userspace;
	uint8_t fase46_entered_exit;
	uint8_t fase46_entered_wait;
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
void process_capture_syscall_frame_at_entry(uint64_t *frame_base);
void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax);

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax);
void fork_ret_emit_pre_return(void);
void fork_restore_emit_pre_iretq(void);
void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw);
int fork_flow_note_debug_exception(uint64_t *stack);
void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall);
__attribute__((noreturn)) void process_exit(int code);
int process_wait(pid_t pid, int *status, int options);

/* IR0 PHILOSOPHY: Only spawn() creates processes - total simplicity
 * Mode must be explicitly specified - no magic address detection */
pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode);

/* Convenience wrappers for explicit mode specification */
pid_t spawn_user(void (*entry)(void), const char *name);
pid_t spawn_kernel(void (*entry)(void), const char *name);

/* Fork exists only for POSIX syscall compatibility - uses spawn() internally */
pid_t fork(void);


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
void process_destroy(process_t *p);
void process_unmap_user_address_space(process_t *p);
int process_remove_from_list(process_t *target);

void process_fase43_proc_audit(const char *tag);
void process_fase43_live_proc_dump(void);
void process_fase43_note_mm_created(void);
void process_fase43_note_mm_destroyed(void);

uint64_t process_list_count(void);
uint64_t process_list_count_user(void);
void process_fase44_list_checkpoint(const char *tag);
void process_fase44_live_summary(const char *tag);
void process_fase44_drain_zombie_children(pid_t ppid);

void process_fase45_fork_audit(const char *tag);
void process_fase45_summary(const char *tag);

void process_fase46_proc_log(process_t *p, int64_t fork_ret, const char *phase);
void process_fase46_note_wait(process_t *p);
void process_fase46_convergence_summary(const char *tag);

void process_fase47_mm_owner_audit(const char *tag);

int64_t process_close_fd(process_t *proc, int fd);
void process_exec_close_cloexec(process_t *p);
void process_fase48_capture_fd_baseline(process_t *p);
void process_fase48_ipc_summary(const char *tag);
uint64_t process_count_open_fds(process_t *p);

extern process_t *current_process;
extern process_t *process_list;
