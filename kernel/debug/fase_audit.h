/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fase_audit.h
 * Description: FASE43–48 bring-up audit API (debug builds only)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/debug_runtime.h>
#include <ir0/types.h>
#include <stddef.h>
#include <stdint.h>

struct process;
typedef struct process process_t;

void process_reap_zombie_child(process_t *child);

typedef enum
{
	FASE44_PROC_ALIVE = 0,
	FASE44_PROC_EXITING,
	FASE44_PROC_ZOMBIE,
	FASE44_PROC_REAPED,
	FASE44_PROC_DESTROYED
} fase44_audit_state_t;

typedef struct fase_proc_audit
{
	process_t *proc;
	uint8_t fase44_audit_state;
	uint32_t fase46_fork_generation;
	pid_t fase46_fork_parent_pid;
	uint8_t fase46_entered_userspace;
	uint8_t fase46_entered_exit;
	uint8_t fase46_entered_wait;
} fase_proc_audit_t;

#if IR0_DEBUG_PROC

fase_proc_audit_t *fase_audit_get(process_t *p, int create);
void fase_audit_unbind(process_t *p);
void fase_audit_spawn_init(process_t *p);
void fase_audit_fork_init(process_t *child, process_t *parent);

void fase_audit_note_proc_created(void);
void fase_audit_note_proc_exited(void);
void fase_audit_note_proc_zombie(void);
void fase_audit_note_proc_destroyed(void);
void fase_audit_note_reparent(void);
void fase_audit_note_reap_event(void);
void fase_audit_note_fork_rollback(void);
void fase_audit_note_scheduled(void);

const char *fase_audit_state_name(int state);
void fase_audit_trace_pid(pid_t pid, const char *event);
void fase_audit_ref_emit(process_t *p, const char *tag);
void fase_audit_fork_state(pid_t pid, const char *stage);
void fase_audit_assert_child_not_visible(pid_t pid);

void fase_audit_destroy_audit(process_t *p, pid_t parent, uint8_t state_before,
			      uint8_t state_after, int removed_from_list,
			      const char *tag);
void fase_audit_reap_zombie(process_t *child, pid_t parent_pid, const char *tag);

void process_fase43_proc_audit(const char *tag);
void process_fase43_live_proc_dump(void);
void process_fase43_note_mm_created(void);
void process_fase43_note_mm_destroyed(void);

void process_fase44_list_checkpoint(const char *tag);
void process_fase44_live_summary(const char *tag);
void process_fase44_drain_zombie_children(pid_t ppid);

void process_fase45_fork_audit(const char *tag);
void process_fase45_summary(const char *tag);

void process_fase46_proc_log(process_t *p, int64_t fork_ret, const char *phase);
void process_fase46_note_wait(process_t *p);
void process_fase46_convergence_summary(const char *tag);

void process_fase47_mm_owner_audit(const char *tag);

void process_fase48_capture_fd_baseline(process_t *p);
void process_fase48_ipc_summary(const char *tag);
uint64_t process_count_open_fds(process_t *p);

#else

static inline fase_proc_audit_t *fase_audit_get(process_t *p, int create)
{
	(void)p;
	(void)create;
	return 0;
}

static inline void fase_audit_unbind(process_t *p) { (void)p; }
static inline void fase_audit_spawn_init(process_t *p) { (void)p; }
static inline void fase_audit_fork_init(process_t *child, process_t *parent)
{
	(void)child;
	(void)parent;
}

static inline void fase_audit_note_proc_created(void) {}
static inline void fase_audit_note_proc_exited(void) {}
static inline void fase_audit_note_proc_zombie(void) {}
static inline void fase_audit_note_proc_destroyed(void) {}
static inline void fase_audit_note_reparent(void) {}
static inline void fase_audit_note_reap_event(void) {}
static inline void fase_audit_note_fork_rollback(void) {}
static inline void fase_audit_note_scheduled(void) {}

static inline const char *fase_audit_state_name(int state)
{
	(void)state;
	return "UNKNOWN";
}

static inline void fase_audit_trace_pid(pid_t pid, const char *event)
{
	(void)pid;
	(void)event;
}

static inline void fase_audit_ref_emit(process_t *p, const char *tag)
{
	(void)p;
	(void)tag;
}

static inline void fase_audit_fork_state(pid_t pid, const char *stage)
{
	(void)pid;
	(void)stage;
}

static inline void fase_audit_assert_child_not_visible(pid_t pid)
{
	(void)pid;
}

static inline void fase_audit_destroy_audit(process_t *p, pid_t parent,
					    uint8_t state_before,
					    uint8_t state_after,
					    int removed_from_list,
					    const char *tag)
{
	(void)p;
	(void)parent;
	(void)state_before;
	(void)state_after;
	(void)removed_from_list;
	(void)tag;
}

static inline void fase_audit_reap_zombie(process_t *child, pid_t parent_pid,
					  const char *tag)
{
	(void)parent_pid;
	(void)tag;
	process_reap_zombie_child(child);
}

static inline void process_fase43_proc_audit(const char *tag) { (void)tag; }
static inline void process_fase43_live_proc_dump(void) {}
static inline void process_fase43_note_mm_created(void) {}
static inline void process_fase43_note_mm_destroyed(void) {}

static inline void process_fase44_list_checkpoint(const char *tag) { (void)tag; }
static inline void process_fase44_live_summary(const char *tag) { (void)tag; }
static inline void process_fase44_drain_zombie_children(pid_t ppid) { (void)ppid; }

static inline void process_fase45_fork_audit(const char *tag) { (void)tag; }
static inline void process_fase45_summary(const char *tag) { (void)tag; }

static inline void process_fase46_proc_log(process_t *p, int64_t fork_ret,
					 const char *phase)
{
	(void)p;
	(void)fork_ret;
	(void)phase;
}

static inline void process_fase46_note_wait(process_t *p) { (void)p; }
static inline void process_fase46_convergence_summary(const char *tag)
{
	(void)tag;
}

static inline void process_fase47_mm_owner_audit(const char *tag) { (void)tag; }

static inline void process_fase48_capture_fd_baseline(process_t *p) { (void)p; }
static inline void process_fase48_ipc_summary(const char *tag) { (void)tag; }

static inline uint64_t process_count_open_fds(process_t *p)
{
	(void)p;
	return 0;
}

#endif
