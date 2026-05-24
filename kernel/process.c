/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.c
 * Description: IR0 kernel Process lifecycle management, fork, exit, wait
 */


#include <ir0/console.h>
#include <ir0/debug_trap.h>
#include "process.h"
#include <config.h>
#include "scheduler_api.h"
#include <ir0/kmem.h>
#include <ir0/pipe.h>
#include <mm/paging.h>
#include <fs/vfs.h>
#include <ir0/serial_io.h>
#include <ir0/video_backend.h>
#include <ir0/permissions.h>
#include <ir0/signals.h>
#include <ir0/oops.h>
#include <string.h>
#include <stddef.h>
#include <ir0/errno.h>
#include <ir0/arch_port.h>
#include <ir0/copy_user.h>
#include <ir0/fcntl.h>
#include <ir0/fase51_debug.h>
#include <mm/pmm.h>
#include <config.h>

extern void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
				uint64_t *blocked_readers, uint64_t *blocked_writers);
extern void pipe_wake_all(pipe_t *pipe);
extern int64_t process_close_fd(process_t *proc, int fd);

static pid_t next_pid = 2;

typedef struct
{
	uint64_t mapped_pages;
	uint64_t freed_pages;
	uint64_t missing_pages;
	uint64_t pdpt_present;
	uint64_t pd_present;
	uint64_t pt_present;
	uint64_t leaf_present;
	uint64_t pml4_freed;
	uint64_t pdpt_freed;
	uint64_t pd_freed;
	uint64_t pt_freed;
	uint64_t leaf_freed;
} process_reclaim_stats_t;

static uint64_t fase43_proc_created;
static uint64_t fase43_proc_exited;
static uint64_t fase43_proc_zombie;
static uint64_t fase43_proc_reaped;
static uint64_t fase43_proc_destroyed;
static uint64_t fase43_mm_created;
static uint64_t fase43_mm_destroyed;
static uint64_t fase43_reparent_events;
static uint64_t fase43_reap_events;

static uint64_t *process_pt_child(uint64_t *table, size_t index);
static void fase50_trace_proc(const char *stage, process_t *p);

static const char *fase43_state_name(process_state_t state)
{
	switch (state)
	{
	case PROCESS_READY:
		return "READY";
	case PROCESS_RUNNING:
		return "RUNNING";
	case PROCESS_BLOCKED:
		return "BLOCKED";
	case PROCESS_ZOMBIE:
		return "ZOMBIE";
	default:
		return "UNKNOWN";
	}
}

static uint64_t fase43_count_vmas(process_t *p)
{
	uint64_t n = 0;
	struct mmap_region *r;

	if (!p)
		return 0;
	for (r = p->mmap_list; r; r = r->next)
		n++;
	return n;
}

static uint64_t fase43_count_user_frames(process_t *p)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !p->page_directory)
		return 0;

	pml4 = p->page_directory;
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];

					if ((ent & PAGE_PRESENT) && (ent & PAGE_USER))
						count++;
				}
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_leaves_in_range(process_t *p, uint64_t start,
						     uint64_t end)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !p->page_directory || end <= start)
		return 0;

	pml4 = p->page_directory;
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];
					uint64_t virt;

					if (!(ent & PAGE_PRESENT) || !(ent & PAGE_USER))
						continue;
					virt = ((uint64_t)i4 << 39) | ((uint64_t)i3 << 30) |
					       ((uint64_t)i2 << 21) | ((uint64_t)i1 << 12);
					if (virt >= start && virt < end)
						count++;
				}
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_page_table_frames(process_t *p)
{
	size_t i4;
	size_t i3;
	size_t i2;
	uint64_t count = 0;
	uint64_t *pml4;

	if (!p || !p->page_directory || !p->owns_page_directory)
		return 0;

	pml4 = p->page_directory;
	count++;
	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		count++;
		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			count++;
			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				count++;
			}
		}
	}
	return count;
}

static uint64_t process_fase47_count_mmap_pages(process_t *p)
{
	uint64_t pages = 0;
	struct mmap_region *r;

	if (!p)
		return 0;
	for (r = p->mmap_list; r; r = r->next)
		pages += (r->length + PAGE_SIZE_4KB - 1) / PAGE_SIZE_4KB;
	return pages;
}

void process_fase47_mm_owner_audit(const char *tag)
{
	process_t *p;

	serial_print("[FASE47][MM_OWNER] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print("\n");

	for (p = process_list; p; p = p->next)
	{
		uint64_t stack_pages = 0;
		uint64_t heap_pages = 0;
		uint64_t leaf_pages = 0;
		uint64_t mmap_pages = 0;
		uint64_t pt_pages = 0;

		if (p->page_directory && p->owns_page_directory)
		{
			leaf_pages = fase43_count_user_frames(p);
			pt_pages = process_fase47_count_page_table_frames(p);
			if (p->stack_size > 0)
				stack_pages = process_fase47_count_leaves_in_range(
					p, p->stack_start,
					p->stack_start + p->stack_size);
			if (p->heap_end > p->heap_start)
				heap_pages = process_fase47_count_leaves_in_range(
					p, p->heap_start, p->heap_end);
		}
		mmap_pages = process_fase47_count_mmap_pages(p);

		serial_print("[FASE47][MM_OWNER] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" state=");
		serial_print(fase43_state_name(p->state));
		serial_print(" has_mm=");
		serial_print((p->page_directory && p->owns_page_directory) ? "1" : "0");
		serial_print(" pml4=");
		serial_print_hex64(p->page_directory ?
				   (uint64_t)(uintptr_t)p->page_directory : 0);
		serial_print(" heap_pages=");
		serial_print_hex64(heap_pages);
		serial_print(" stack_pages=");
		serial_print_hex64(stack_pages);
		serial_print(" mmap_pages=");
		serial_print_hex64(mmap_pages);
		serial_print(" leaf_pages=");
		serial_print_hex64(leaf_pages);
		serial_print(" page_table_pages=");
		serial_print_hex64(pt_pages);
		serial_print("\n");
	}
}

void process_fase43_note_mm_created(void)
{
	fase43_mm_created++;
}

void process_fase43_note_mm_destroyed(void)
{
	fase43_mm_destroyed++;
}

void process_fase43_proc_audit(const char *tag)
{
	serial_print("[FASE43][PROC_AUDIT] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" proc_created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" proc_exited=");
	serial_print_hex64(fase43_proc_exited);
	serial_print(" proc_zombie=");
	serial_print_hex64(fase43_proc_zombie);
	serial_print(" proc_reaped=");
	serial_print_hex64(fase43_proc_reaped);
	serial_print(" proc_destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" mm_created=");
	serial_print_hex64(fase43_mm_created);
	serial_print(" mm_destroyed=");
	serial_print_hex64(fase43_mm_destroyed);
	serial_print(" reparent=");
	serial_print_hex64(fase43_reparent_events);
	serial_print(" auto_reap=");
	serial_print_hex64(fase43_reap_events);
	serial_print("\n");
}

void process_fase43_live_proc_dump(void)
{
	process_t *p;
	uint64_t live = 0;

	for (p = process_list; p; p = p->next)
	{
		live++;
		serial_print("[FASE43][LIVE_PROC] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" state=");
		serial_print(fase43_state_name(p->state));
		serial_print(" has_mm=");
		serial_print((p->page_directory && p->owns_page_directory) ? "1" : "0");
		serial_print(" has_pml4=");
		serial_print(p->page_directory ? "1" : "0");
		serial_print(" vma_count=");
		serial_print_hex64(fase43_count_vmas(p));
		serial_print(" frame_count=");
		serial_print_hex64(fase43_count_user_frames(p));
		serial_print("\n");
	}

	serial_print("[FASE43][LIVE_PROC] live_processes_final=");
	serial_print_hex64(live);
	serial_print("\n");
	process_fase43_proc_audit("live-proc-dump");
	paging_fase43_oom_audit("live-proc-dump");
}

static int fase44_baseline_set;
static uint64_t fase44_baseline_count;

static const char *fase44_audit_name(uint8_t st)
{
	switch (st)
	{
	case FASE44_PROC_ALIVE:
		return "ALIVE";
	case FASE44_PROC_EXITING:
		return "EXITING";
	case FASE44_PROC_ZOMBIE:
		return "ZOMBIE";
	case FASE44_PROC_REAPED:
		return "REAPED";
	case FASE44_PROC_DESTROYED:
		return "DESTROYED";
	default:
		return "UNKNOWN";
	}
}

static uint64_t fase44_ref_count_files(process_t *p)
{
	uint64_t n = 0;
	int i;

	if (!p)
		return 0;
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (p->fd_table[i].in_use)
			n++;
	}
	return n;
}

static void fase44_ref_emit(process_t *p, const char *tag)
{
	if (!p)
		return;

	serial_print("[FASE44][REF] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" ref_process=1 ref_mm=");
	serial_print((p->page_directory && p->owns_page_directory) ? "1" : "0");
	serial_print(" ref_files=");
	serial_print_hex64(fase44_ref_count_files(p));
	serial_print(" ref_threads=1 audit=");
	serial_print(fase44_audit_name(p->fase44_audit_state));
	serial_print("\n");
}

static void fase44_destroy_audit(process_t *p, pid_t parent, uint8_t state_before,
				 uint8_t state_after, int removed_from_list,
				 const char *tag)
{
	if (!p)
		return;

	serial_print("[FASE44][DESTROY] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" parent=");
	serial_print_hex32((uint32_t)parent);
	serial_print(" state_before=");
	serial_print(fase44_audit_name(state_before));
	serial_print(" state_after=");
	serial_print(fase44_audit_name(state_after));
	serial_print(" had_mm=");
	serial_print((p->page_directory && p->owns_page_directory) ? "1" : "0");
	serial_print(" had_files=");
	serial_print(fase44_ref_count_files(p) > 0 ? "1" : "0");
	serial_print(" had_threads=1 removed_from_list=");
	serial_print(removed_from_list ? "1" : "0");
	serial_print("\n");
}

static void fase44_trace(pid_t pid, const char *event)
{
	serial_print("[FASE44][TRACE] pid=");
	serial_print_hex32((uint32_t)pid);
	serial_print(" ");
	serial_print(event);
	serial_print("\n");
}

static void fase44_reap_zombie(process_t *child, pid_t parent_pid, const char *tag)
{
	uint8_t before;
	int removed;

	if (!child)
		return;

	before = child->fase44_audit_state;
	removed = process_remove_from_list(child);
	fase50_trace_proc("reap_zombie-removed", child);
	fase44_destroy_audit(child, parent_pid, before, FASE44_PROC_REAPED,
			     removed == 0, tag);
	if (removed != 0)
		return;

	fase44_trace(child->task.pid, "REAP");
	child->fase44_audit_state = FASE44_PROC_REAPED;
	fase44_ref_emit(child, tag);
	fase43_proc_reaped++;
	fase44_trace(child->task.pid, "DESTROY");
	fase50_trace_proc("reap_zombie-before-destroy", child);
	process_destroy(child);
	child->fase44_audit_state = FASE44_PROC_DESTROYED;
	fase44_trace(child->task.pid, "FREE");
	kfree(child);
}

uint64_t process_list_count(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
		n++;
	return n;
}

uint64_t process_list_count_user(void)
{
	process_t *p;
	uint64_t n = 0;

	for (p = process_list; p; p = p->next)
	{
		if (p->mode == USER_MODE && p->state != PROCESS_ZOMBIE)
			n++;
	}
	return n;
}

static int fase44_steady_summary_done;

static uint64_t fase45_fork_rollback;

static int fase48_ipc_summary_done;
static uint64_t fase48_fd_baseline;
static int fase47_closure_done;

static uint64_t fase50_count_open_fds(process_t *p)
{
#if CONFIG_DEBUG_FASE50
	uint64_t n = 0;
	int i;

	if (!p)
		return 0;

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (p->fd_table[i].in_use)
			n++;
	}
	return n;
#else
	(void)p;
	return 0;
#endif
}

static void fase50_trace_proc(const char *stage, process_t *p)
{
#if CONFIG_DEBUG_FASE50
	serial_print("[FASE50][TRACE] stage=");
	serial_print(stage ? stage : "(null)");
	serial_print(" current=");
	serial_print_hex64((uint64_t)(uintptr_t)current_process);
	serial_print(" proc=");
	serial_print_hex64((uint64_t)(uintptr_t)p);
	serial_print(" pid=");
	serial_print_hex32(p ? (uint32_t)p->task.pid : 0);
	serial_print(" state=");
	serial_print(p ? fase43_state_name(p->state) : "NULL");
	serial_print(" mm=");
	serial_print_hex64(p ? (uint64_t)(uintptr_t)p->page_directory : 0);
	serial_print(" files=");
	serial_print_hex64(p ? (uint64_t)(uintptr_t)p->fd_table : 0);
	serial_print(" sf=");
	serial_print_hex64(p ? (uint64_t)(uintptr_t)&p->syscall_frame : 0);
	serial_print(" sf_rip=");
	serial_print_hex64(p ? p->syscall_frame.rip : 0);
	serial_print(" sf_rsp=");
	serial_print_hex64(p ? p->syscall_frame.rsp : 0);
	serial_print(" sf_rflags=");
	serial_print_hex64(p ? p->syscall_frame.rflags : 0);
	serial_print(" fds_open=");
	serial_print_hex64(fase50_count_open_fds(p));
	serial_print(" task_cr3=");
	serial_print_hex64(p ? p->task.cr3 : 0);
	serial_print(" task_rip=");
	serial_print_hex64(p ? p->task.rip : 0);
	serial_print(" task_rsp=");
	serial_print_hex64(p ? p->task.rsp : 0);
	serial_print(" task_cs=");
	serial_print_hex64(p ? p->task.cs : 0);
	serial_print(" task_ss=");
	serial_print_hex64(p ? p->task.ss : 0);
	serial_print(" active_cr3=");
	serial_print_hex64(get_current_page_directory());
	serial_print("\n");
#else
	(void)stage;
	(void)p;
#endif
}

static void wait_exit_audit_classify_user_frame(const char *tag, process_t *p)
{
	uint64_t rip;
	uint64_t rsp;
	uint16_t cs;
	uint16_t ss;

	if (!p)
		return;

	rip = p->task.rip;
	rsp = p->task.rsp;
	cs = p->task.cs;
	ss = p->task.ss;

	serial_print("[WAIT_EXIT_AUDIT][FRAME] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" comm=");
	serial_print(p->comm);
	serial_print(" rip=");
	serial_print_hex64(rip);
	serial_print(" rsp=");
	serial_print_hex64(rsp);
	serial_print(" cs=");
	serial_print_hex64((uint64_t)cs);
	serial_print(" ss=");
	serial_print_hex64((uint64_t)ss);
	serial_print(" rflags=");
	serial_print_hex64(p->task.rflags);
	serial_print(" rax=");
	serial_print_hex64(p->task.rax);
	serial_print(" cr3=");
	serial_print_hex64(p->task.cr3);
	serial_print(" irq_saved=");
	serial_print_hex64((uint64_t)p->irq_frame_saved);
	serial_print("\n");

	if (rip < 0x00400000ULL || rip > 0x00007FFFFFFFFFFFULL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RIP\n");
	}
	if (rsp < 0x00400000ULL || rsp > 0x00007FFFFFFFFFFFULL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_RSP\n");
	}
	if (cs != (uint16_t)USER_CODE_SEL || ss != (uint16_t)USER_DATA_SEL)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] PARENT_IRET_FRAME_BAD_CS_SS\n");
	}
}

static void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
                                         int parent_state_before)
{
	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" child_state_before=RUNNING child_state_after=ZOMBIE parent_pid=");
	serial_print_hex32(parent ? (uint32_t)parent->task.pid : 0);
	serial_print(" parent_state_before=");
	serial_print_hex64((uint64_t)(unsigned int)parent_state_before);
	serial_print(" parent_state_after=");
	serial_print_hex64(parent ? (uint64_t)(unsigned int)parent->state : 0);
	serial_print(" parent_woken=");
	serial_print_hex64((uint64_t)(parent && parent_state_before == PROCESS_BLOCKED &&
	                             parent->state == PROCESS_READY ? 1 : 0));
	serial_print("\n");

	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_frees address_space=0 page_directory=0 "
	             "task_struct=0 kernel_stack=0 (zombie until reap)\n");
	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_closes fd_table=1 cwd=0 vfs_mm=0\n");

	if (parent)
	{
		serial_print("[WAIT_EXIT_AUDIT][process_exit] parent_mm=");
		serial_print_hex64((uint64_t)(uintptr_t)parent->page_directory);
		serial_print(" parent_cr3=");
		serial_print_hex64(parent->task.cr3);
		serial_print(" parent_files=");
		serial_print_hex64((uint64_t)(uintptr_t)parent->fd_table);
		serial_print(" parent_irq_saved=");
		serial_print_hex64((uint64_t)parent->irq_frame_saved);
		serial_print("\n");
		wait_exit_audit_classify_user_frame("parent-at-child-exit", parent);
	}

	serial_print("[WAIT_EXIT_AUDIT][process_exit] child_irq_saved=");
	serial_print_hex64((uint64_t)dying->irq_frame_saved);
	serial_print("\n");
	if (dying->irq_frame_saved)
	{
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] EXIT_FREED_PARENT_RESOURCE "
		             "note=child_irq_saved_stale_may_misroute_resume\n");
	}
}

static void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status)
{
	if (!current_process)
		return;

	serial_print("[WAIT_EXIT_AUDIT][process_wait] action=block parent_pid=");
	serial_print_hex32((uint32_t)current_process->task.pid);
	serial_print(" wait_pid=");
	serial_print_hex32((uint32_t)wait_pid);
	serial_print(" status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)status);
	serial_print(" expected_rax_after_wake=child_pid\n");
	wait_exit_audit_classify_user_frame("parent-before-wait-sleep", current_process);
	serial_print("[WAIT_EXIT_AUDIT][process_wait] syscall_frame rip=");
	serial_print_hex64(current_process->syscall_frame.rip);
	serial_print(" rsp=");
	serial_print_hex64(current_process->syscall_frame.rsp);
	serial_print("\n");
}

static void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status)
{
	if (!current_process)
		return;

	serial_print("[WAIT_EXIT_AUDIT][process_wait] action=reap parent_pid=");
	serial_print_hex32((uint32_t)current_process->task.pid);
	serial_print(" reaped_pid=");
	serial_print_hex32((uint32_t)reaped_pid);
	serial_print(" status_val=");
	serial_print_hex64((uint64_t)(unsigned int)status_val);
	serial_print(" status_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)status);
	serial_print(" return_rax=");
	serial_print_hex64((uint64_t)(unsigned int)reaped_pid);
	serial_print("\n");
	wait_exit_audit_classify_user_frame("parent-after-reap", current_process);
}

static void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	int i;

	if (!p)
		return;
	fase50_trace_proc("process_release_fds-begin", p);

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &p->fd_table[i];

		if (!e->in_use)
			continue;

		if (i >= 1000 && i <= 3999)
			goto clear_fd;

		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;
			int refs_before = pip ? pip->fd_refs : -1;

			if (pipe_trace_op)
			{
				pipe_fase49_fd_trace((uint32_t)p->task.pid, i, pip,
						     e->pipe_end, pip->fd_refs,
						     pipe_trace_op);
			}
			serial_print("[FASE50][FDREL] stage=close_pipe pid=");
			serial_print_hex32((uint32_t)p->task.pid);
			serial_print(" fd=");
			serial_print_hex64((uint64_t)i);
			serial_print(" refs_before=");
			serial_print_hex64((uint64_t)refs_before);
			serial_print(" end=");
			serial_print_hex64((uint64_t)e->pipe_end);
			serial_print("\n");
			pipe_close_end(pip, e->pipe_end);
			pipe_wake_all(pip);
			e->vfs_file = NULL;
		}
		else if (i <= 2)
			goto clear_fd;
		else if (e->vfs_file)
		{
			vfs_close((struct vfs_file *)e->vfs_file);
			e->vfs_file = NULL;
		}

clear_fd:
		e->in_use = false;
		e->is_pipe = false;
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}
	fase50_trace_proc("process_release_fds-end", p);
}

void process_exec_close_cloexec(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 3; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &p->fd_table[i];

		if (!e->in_use)
			continue;
		if (!(e->fd_flags & FD_CLOEXEC))
			continue;
		if (e->is_pipe && e->vfs_file)
		{
			pipe_t *pip = (pipe_t *)e->vfs_file;

			pipe_fase49_fd_trace((uint32_t)p->task.pid, i, pip, e->pipe_end,
					     pip->fd_refs, "EXEC_CLOSE");
		}
		(void)process_close_fd(p, i);
	}
}

void process_fase48_capture_fd_baseline(process_t *p)
{
	if (!fase48_fd_baseline && p)
		fase48_fd_baseline = process_count_open_fds(p);
}

void process_fase48_ipc_summary(const char *tag)
{
	uint64_t pipe_created = 0;
	uint64_t pipe_destroyed = 0;
	uint64_t fd_created = 0;
	uint64_t fd_destroyed = 0;
	uint64_t blocked_readers = 0;
	uint64_t blocked_writers = 0;
	uint64_t live_user = process_list_count_user();
	uint64_t fd_after = 0;
	process_t *init;
	int inv_pipe;
	int inv_fd;
	int inv_proc;
	const char *ipc_class;

	if (fase48_ipc_summary_done)
		return;
	fase48_ipc_summary_done = 1;

	init = process_find_by_pid(1);
	if (init)
		fd_after = process_count_open_fds(init);

	pipe_fase48_get_stats(&pipe_created, &pipe_destroyed);
	fase48_fd_get_stats(&fd_created, &fd_destroyed, &blocked_readers,
			    &blocked_writers);

	if (!fase48_fd_baseline && init)
		fase48_fd_baseline = process_count_open_fds(init);

	inv_pipe = (pipe_created == pipe_destroyed);
	inv_fd = (fd_created == fd_destroyed);
	inv_proc = (fase43_proc_created == fase43_proc_destroyed + live_user);

	serial_print("[FASE48][IPC] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" fd_before=");
	serial_print_hex64(fase48_fd_baseline);
	serial_print(" fd_after=");
	serial_print_hex64(fd_after);
	serial_print(" pipe_created=");
	serial_print_hex64(pipe_created);
	serial_print(" pipe_destroyed=");
	serial_print_hex64(pipe_destroyed);
	serial_print(" fd_created=");
	serial_print_hex64(fd_created);
	serial_print(" fd_destroyed=");
	serial_print_hex64(fd_destroyed);
	serial_print(" blocked_readers=");
	serial_print_hex64(blocked_readers);
	serial_print(" blocked_writers=");
	serial_print_hex64(blocked_writers);
	serial_print(" proc_created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" proc_destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" proc_live=");
	serial_print_hex64(live_user);
	serial_print(" pipe_inv=");
	serial_print(inv_pipe ? "OK" : "FAIL");
	serial_print(" fd_inv=");
	serial_print(inv_fd ? "OK" : "FAIL");
	serial_print(" proc_inv=");
	serial_print(inv_proc ? "OK" : "FAIL");
	serial_print("\n");

	if (!inv_pipe || !inv_fd)
		ipc_class = "IPC_LEAK";
	else if (!inv_proc)
		ipc_class = "FD_LIFECYCLE_BROKEN";
	else
		ipc_class = "IPC_READY";

	serial_print("[FASE48][CLASS] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" ipc_class=");
	serial_print(ipc_class);
	serial_print("\n");
}

uint64_t process_count_open_fds(process_t *p)
{
	uint64_t n = 0;
	int i;

	if (!p)
		return 0;

	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		if (p->fd_table[i].in_use)
			n++;
	}
	return n;
}

static uint64_t fase46_scheduled;
static uint64_t fase46_entered_userspace;
static uint64_t fase46_child_user_enter;
static uint64_t fase46_exited;
static uint64_t fase46_child_exited;
static uint64_t fase46_entered_wait;
static uint64_t fase46_child_destroyed;
static uint64_t fase46_frames_baseline;
static int fase46_frames_baseline_set;

static void process_fase47_closure_audit(const char *tag)
{
	process_fase47_mm_owner_audit(tag);
	paging_fase47_steady_state_audit(tag, fase46_frames_baseline,
					 fase43_mm_created, fase43_mm_destroyed);
}

static void fase45_fork_state(pid_t pid, const char *stage)
{
	serial_print("[FASE45][FORK_STATE] pid=");
	serial_print_hex32((uint32_t)pid);
	serial_print(" stage=");
	serial_print(stage ? stage : "(null)");
	serial_print("\n");
}

static void fase45_assert_child_not_visible(pid_t pid)
{
	if (process_find_by_pid(pid) != NULL)
	{
		serial_print("[FASE45][ASSERT] child visible after rollback pid=");
		serial_print_hex32((uint32_t)pid);
		serial_print("\n");
	}
}

void process_fase45_fork_audit(const char *tag)
{
	serial_print("[FASE45][FORK_AUDIT] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" rollback=");
	serial_print_hex64(fase45_fork_rollback);
	serial_print(" proc_created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" proc_destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" visible=");
	serial_print_hex64(process_list_count());
	serial_print(" user_live=");
	serial_print_hex64(process_list_count_user());
	serial_print("\n");
}

void process_fase45_summary(const char *tag)
{
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t baseline = fase44_baseline_count;
	int lifecycle_ok;

	lifecycle_ok = (fase43_proc_created == fase43_proc_destroyed + live_user &&
			live <= baseline + 2);

	serial_print("[FASE45][SUMMARY] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" rollback=");
	serial_print_hex64(fase45_fork_rollback);
	serial_print(" visible_processes=");
	serial_print_hex64(live);
	serial_print(" baseline=");
	serial_print_hex64(baseline);
	serial_print(" live_user=");
	serial_print_hex64(live_user);
	serial_print(" class=");
	serial_print(lifecycle_ok ? "FORK_ROLLBACK_OK" : "FORK_ROLLBACK_BROKEN");
	serial_print("\n");
	process_fase45_fork_audit(tag);
}

void process_fase46_proc_log(process_t *p, int64_t fork_ret, const char *phase)
{
	if (!p || !phase)
		return;

	serial_print("[FASE46][PROC] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" ppid=");
	serial_print_hex32((uint32_t)p->ppid);
	serial_print(" gen=");
	serial_print_hex32(p->fase46_fork_generation);
	serial_print(" fork_ret=");
	serial_print_hex64((uint64_t)fork_ret);
	serial_print(" phase=");
	serial_print(phase);
	serial_print(" rip=");
	serial_print_hex64(p->task.rip);
	serial_print("\n");

	if (strcmp(phase, "USER_ENTER") == 0)
	{
		if (p->fase46_entered_userspace)
		{
			serial_print("[FASE46][ASSERT] duplicate USER_ENTER pid=");
			serial_print_hex32((uint32_t)p->task.pid);
			serial_print("\n");
		}
		p->fase46_entered_userspace = 1;
		fase46_entered_userspace++;
		if (p->fase46_fork_generation > 0)
			fase46_child_user_enter++;
	}
	else if (strcmp(phase, "EXIT") == 0)
	{
		p->fase46_entered_exit = 1;
		fase46_exited++;
		if (p->fase46_fork_generation > 0)
			fase46_child_exited++;
	}
	else if (strcmp(phase, "DESTROY") == 0)
	{
		if (p->fase46_fork_generation > 0)
			fase46_child_destroyed++;
	}
}

void process_fase46_note_wait(process_t *p)
{
	if (!p || p->fase46_entered_wait)
		return;

	p->fase46_entered_wait = 1;
	fase46_entered_wait++;
	process_fase46_proc_log(p, -1, "WAIT");
}

void process_fase46_convergence_summary(const char *tag)
{
	size_t total_frames = 0;
	size_t used_frames = 0;
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t baseline = fase44_baseline_count;
	int inv_scheduled;
	int inv_user_enter;
	int inv_destroyed;
	int inv_visible;
	int inv_frames;
	int convergence_ok;

	if (!fase46_frames_baseline_set)
	{
		pmm_stats(&total_frames, &used_frames, NULL);
		fase46_frames_baseline = (uint64_t)used_frames;
		fase46_frames_baseline_set = 1;
	}

	pmm_stats(&total_frames, &used_frames, NULL);

	inv_scheduled = (fase46_scheduled <= fase43_proc_created);
	inv_user_enter = (fase46_entered_userspace <= fase46_scheduled);
	inv_destroyed = (fase43_proc_destroyed + live_user == fase43_proc_created);
	inv_visible = (live <= baseline + 2);
	inv_frames = (fase46_frames_baseline == 0 ||
		      (uint64_t)used_frames <= (fase46_frames_baseline * 105ULL / 100ULL));

	convergence_ok = inv_scheduled && inv_user_enter && inv_destroyed &&
			 inv_visible && inv_frames;

	serial_print("[FASE46][CONVERGENCE] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" scheduled=");
	serial_print_hex64(fase46_scheduled);
	serial_print(" entered_userspace=");
	serial_print_hex64(fase46_entered_userspace);
	serial_print(" exited=");
	serial_print_hex64(fase46_exited);
	serial_print(" reaped=");
	serial_print_hex64(fase43_proc_reaped);
	serial_print(" destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" visible=");
	serial_print_hex64(live);
	serial_print(" user_live=");
	serial_print_hex64(live_user);
	serial_print(" frames_before=");
	serial_print_hex64(fase46_frames_baseline);
	serial_print(" frames_after=");
	serial_print_hex64((uint64_t)used_frames);
	serial_print(" children_entered=");
	serial_print_hex64(fase46_child_user_enter);
	serial_print(" children_exit=");
	serial_print_hex64(fase46_child_exited);
	serial_print(" children_destroy=");
	serial_print_hex64(fase46_child_destroyed);
	serial_print(" class=");
	serial_print(convergence_ok ? "PROCESS_CONVERGENCE_OK" : "PROCESS_CONVERGENCE_BROKEN");
	serial_print("\n");

	serial_print("[FASE46][FEATURE] fork_return_semantics=");
	serial_print(inv_user_enter ? "OK" : "FAIL");
	serial_print(" child_execution_isolation=");
	serial_print((fase46_child_user_enter <= fase46_scheduled) ? "OK" : "FAIL");
	serial_print(" wait_lifecycle=");
	serial_print(fase46_entered_wait > 0 ? "OK" : "FAIL");
	serial_print(" exit_lifecycle=");
	serial_print((fase46_exited >= fase43_proc_reaped) ? "OK" : "FAIL");
	serial_print(" destroy_lifecycle=");
	serial_print(inv_destroyed ? "OK" : "FAIL");
	serial_print(" heap_post_fork=");
	serial_print("AUDIT");
	serial_print(" steady_state=");
	serial_print((inv_visible && inv_destroyed) ? "OK" : "FAIL");
	serial_print("\n");

	if (!fase47_closure_done)
	{
		fase47_closure_done = 1;
		process_fase47_closure_audit(tag);
	}
}

static void fase44_maybe_steady_summary(const char *tag)
{
	process_t *p;
	uint64_t zombies = 0;

	if (fase44_steady_summary_done || !fase44_baseline_set)
		return;
	if (process_list_count() != fase44_baseline_count)
		return;

	for (p = process_list; p; p = p->next)
	{
		if (p->state == PROCESS_ZOMBIE)
			zombies++;
	}
	if (zombies != 0)
		return;

	fase44_steady_summary_done = 1;
	process_fase44_live_summary(tag ? tag : "steady-state");
	process_fase45_summary(tag ? tag : "steady-state");
	process_fase46_convergence_summary(tag ? tag : "steady-state");
}

void process_fase44_list_checkpoint(const char *tag)
{
	uint64_t cnt = process_list_count();

	if (!fase44_baseline_set && current_process &&
	    current_process->task.pid == 1)
	{
		fase44_baseline_count = cnt;
		fase44_baseline_set = 1;
	}

	serial_print("[FASE44][LIST] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" count=");
	serial_print_hex64(cnt);
	serial_print(" baseline=");
	serial_print_hex64(fase44_baseline_count);
	serial_print(" user_live=");
	serial_print_hex64(process_list_count_user());
	serial_print("\n");

	if (tag && (strncmp(tag, "wait-after", 10) == 0 ||
		    strncmp(tag, "drain-zombie-after", 18) == 0))
	{
		fase44_maybe_steady_summary(tag);
		if (!fase47_closure_done && fase44_baseline_set &&
		    process_list_count() == fase44_baseline_count &&
		    process_list_count_user() <= 1)
		{
			fase47_closure_done = 1;
			process_fase47_closure_audit(tag);
		}
	}
}

void process_fase44_drain_zombie_children(pid_t ppid)
{
	process_t *child;

	for (;;)
	{
		process_t *found = NULL;

		for (child = process_list; child; child = child->next)
		{
			if (child->ppid == ppid && child->state == PROCESS_ZOMBIE &&
			    child->task.pid != ppid)
			{
				found = child;
				break;
			}
		}
		if (!found)
			return;

		fase43_reap_events++;
		process_fase43_proc_audit("drain-zombie");
		fase44_reap_zombie(found, ppid, "drain-zombie");
		process_fase44_list_checkpoint("drain-zombie-after");
	}
}

void process_fase44_live_summary(const char *tag)
{
	process_t *p;
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t zombies = 0;
	int lifecycle_ok;

	for (p = process_list; p; p = p->next)
	{
		if (p->state == PROCESS_ZOMBIE)
			zombies++;

		serial_print("[FASE44][LIVE] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" comm=");
		serial_print(p->comm);
		serial_print(" state=");
		serial_print(fase43_state_name(p->state));
		serial_print(" audit=");
		serial_print(fase44_audit_name(p->fase44_audit_state));
		serial_print("\n");
	}

	lifecycle_ok = (live == fase44_baseline_count && zombies == 0 &&
			fase43_proc_created == fase43_proc_destroyed + 1);

	serial_print("[FASE44][SUMMARY] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" created=");
	serial_print_hex64(fase43_proc_created);
	serial_print(" destroyed=");
	serial_print_hex64(fase43_proc_destroyed);
	serial_print(" zombie=");
	serial_print_hex64(fase43_proc_zombie);
	serial_print(" reaped=");
	serial_print_hex64(fase43_proc_reaped);
	serial_print(" mm_created=");
	serial_print_hex64(fase43_mm_created);
	serial_print(" mm_destroyed=");
	serial_print_hex64(fase43_mm_destroyed);
	serial_print(" process_count_before=");
	serial_print_hex64(fase44_baseline_count);
	serial_print(" process_count_after=");
	serial_print_hex64(live);
	serial_print(" live_user_processes_final=");
	serial_print_hex64(live_user);
	serial_print(" zombies_on_list=");
	serial_print_hex64(zombies);
	serial_print(" class=");
	serial_print(lifecycle_ok ? "PROCESS_LIFECYCLE_OK" : "PROCESS_REAP_BROKEN");
	serial_print("\n");

	serial_print("[FASE44][FEATURE] wait_cleanup=");
	serial_print(fase43_proc_reaped > 0 ? "OK" : "FAIL");
	serial_print(" zombie_destroy=");
	serial_print(zombies == 0 ? "OK" : "FAIL");
	serial_print(" process_list_cleanup=");
	serial_print(live == fase44_baseline_count ? "OK" : "FAIL");
	serial_print(" mm_release=");
	serial_print(fase43_mm_created == fase43_mm_destroyed ? "OK" : "FAIL");
	serial_print(" steady_state=");
	serial_print((live == fase44_baseline_count && zombies == 0 &&
		      fase43_proc_created == fase43_proc_destroyed + 1) ? "OK" : "FAIL");
	serial_print("\n");
}

static inline uint64_t process_irq_save(void)
{
#if defined(__x86_64__) || defined(__i386__)
	uint64_t flags;
	__asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
	return flags;
#else
	arch_disable_interrupts();
	return 0;
#endif
}

static inline void process_irq_restore(uint64_t flags)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ volatile("pushq %0; popfq" :: "r"(flags) : "memory", "cc");
#else
	(void)flags;
	arch_enable_interrupts();
#endif
}

/*
 * Follow one level of the page table hierarchy; returns NULL if the entry is
 * absent or a huge page (unmap path only supports 4KB walks).
 */
static uint64_t *process_pt_child(uint64_t *table, size_t index)
{
	if (!(table[index] & PAGE_PRESENT))
		return NULL;
	if (table[index] & PAGE_SIZE_2MB_FLAG)
		return NULL;
	return (uint64_t *)(table[index] & PAGE_FRAME_MASK);
}

/*
 * Drop every present PAGE_USER mapping under PML4 indices 0..255 so PMM
 * frames are returned and the address space can be discarded safely while
 * another process is active (CR3 unrelated).
 */
static void process_unmap_user_pages_all(uint64_t *pml4,
					 process_reclaim_stats_t *stats)
{
	size_t i4;
	size_t i3;
	size_t i2;
	size_t i1;

	if (!pml4)
		return;

	for (i4 = 0; i4 < 256; i4++)
	{
		uint64_t *pdpt = process_pt_child(pml4, i4);

		if (!pdpt)
			continue;
		if (stats)
			stats->pdpt_present++;

		for (i3 = 0; i3 < 512; i3++)
		{
			uint64_t *pd = process_pt_child(pdpt, i3);

			if (!pd)
				continue;
			if (stats)
				stats->pd_present++;

			for (i2 = 0; i2 < 512; i2++)
			{
				uint64_t *pt = process_pt_child(pd, i2);

				if (!pt)
					continue;
				if (stats)
					stats->pt_present++;

				for (i1 = 0; i1 < 512; i1++)
				{
					uint64_t ent = pt[i1];
					uintptr_t virt;

					if (!(ent & PAGE_PRESENT) || !(ent & PAGE_USER))
						continue;
					if (stats)
					{
						stats->mapped_pages++;
						stats->leaf_present++;
					}

					virt = ((uintptr_t)i4 << 39) | ((uintptr_t)i3 << 30) |
					       ((uintptr_t)i2 << 21) | ((uintptr_t)i1 << 12);
					if (unmap_page_in_directory(pml4, virt) == 0)
					{
						if (stats)
						{
							stats->freed_pages++;
							stats->leaf_freed++;
						}
					}
					else if (stats)
					{
						stats->missing_pages++;
					}
				}
			}
		}
	}
}


void process_unmap_user_address_space(process_t *p)
{
	process_reclaim_stats_t stats;
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;
	memset(&stats, 0, sizeof(stats));

	process_unmap_user_pages_all(p->page_directory, &stats);
	serial_print("[FASE41][UNMAP_ALL] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" mapped_pages=");
	serial_print_hex64(stats.mapped_pages);
	serial_print(" freed_pages=");
	serial_print_hex64(stats.freed_pages);
	serial_print(" missing_pages=");
	serial_print_hex64(stats.missing_pages);
	serial_print(" pdpt_present=");
	serial_print_hex64(stats.pdpt_present);
	serial_print(" pd_present=");
	serial_print_hex64(stats.pd_present);
	serial_print(" pt_present=");
	serial_print_hex64(stats.pt_present);
	serial_print(" pt_freed=");
	serial_print_hex64(stats.pt_freed);
	serial_print("\n");
	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	serial_print("[FASE41][PMM_AUDIT] orphan_frames=");
	serial_print_hex64(orphan_frames);
	serial_print(" double_free=");
	serial_print_hex64(double_free);
	serial_print(" alive_owner_missing=");
	serial_print_hex64(alive_owner_missing);
	serial_print("\n");
}


process_t *current_process = NULL;
process_t *process_list = NULL;


void process_init(void)
{
	current_process = NULL;
	process_list = NULL;
	ir0_debug_trap_init();
#if KERNEL_DEBUG_SHELL
	/* PID 1 reserved for debug-shell init (start_init_process hardcodes pid 1). */
	next_pid = 2;
#else
	/* First spawned process is /sbin/init (PID 1). */
	next_pid = 1;
#endif

	/* Initialize simple user system */
	init_simple_users();
}


pid_t process_get_next_pid(void)
{
	uint64_t irq_flags = process_irq_save();
	pid_t pid = next_pid++;
	process_irq_restore(irq_flags);
	return pid;
}

process_t *process_get_current(void)
{
	return current_process;
}

/*
 * irq_save_user_frame - Copy user iretq frame from IRQ stack into current task.
 * @frame: isr_handler64 stack base (int_no, err, RIP, CS, RFLAGS, RSP, SS).
 */
void irq_save_user_frame(uint64_t *frame)
{
	process_t *p;

	if (!frame)
		return;

	p = current_process;
	if (!p || p->mode != USER_MODE)
		return;

	if ((frame[3] & 3U) != 3U)
		return;

#if CONFIG_DEBUG_ISRABI
	serial_print("[ISRABI][IRQ_SAVE] pid=");
	serial_print_hex32(current_process ? (uint32_t)current_process->task.pid : 0);
	serial_print(" src_int=");
	serial_print_hex64(frame[0]);
	serial_print(" src_err=");
	serial_print_hex64(frame[1]);
	serial_print(" src_rip=");
	serial_print_hex64(frame[2]);
	serial_print(" src_cs=");
	serial_print_hex64(frame[3]);
	serial_print(" src_rflags=");
	serial_print_hex64(frame[4]);
	serial_print(" src_rsp=");
	serial_print_hex64(frame[5]);
	serial_print(" src_ss=");
	serial_print_hex64(frame[6]);
	serial_print("\n");
#endif

	p->task.rip = frame[2];
	p->task.rflags = ir0_rflags_sanitize_user((frame[4] | 2ULL) | RFLAGS_IF);
	p->task.rsp = frame[5];
	if ((frame[3] & 3U) == 3U)
	{
		p->task.cs = (uint16_t)USER_CODE_SEL;
		p->task.ss = (uint16_t)USER_DATA_SEL;
	}
	else
	{
		p->task.cs = (uint16_t)frame[3];
		p->task.ss = (uint16_t)frame[6];
	}
	/* irq_frame_saved is set only when blocking in process_wait(), not on timer IRQ. */

#if CONFIG_DEBUG_ISRABI
	serial_print("[ISRABI][IRQ_SAVE] task_rip=");
	serial_print_hex64(p->task.rip);
	serial_print(" task_rsp=");
	serial_print_hex64(p->task.rsp);
	serial_print(" task_cs=");
	serial_print_hex64((uint64_t)p->task.cs);
	serial_print(" task_ss=");
	serial_print_hex64((uint64_t)p->task.ss);
	serial_print(" task_rflags=");
	serial_print_hex64(p->task.rflags);
	serial_print("\n");
#endif
}

pid_t process_get_pid(void)
{
	return current_process ? process_pid(current_process) : 0;
}

pid_t process_get_ppid(void)
{
	return current_process ? current_process->ppid : 0;
}

process_t *get_process_list(void)
{
	return process_list;
}



/* 
 * spawn() - Create a new process (IR0's ONLY process creation method)
 * 
 * IR0 PHILOSOPHY: Total simplicity with explicit mode specification
 * Only spawn() creates processes. No fork(), no clone(), no other methods.
 * Mode must be explicitly specified - no magic address detection.
 * 
 * This avoids fragile heuristics based on memory layout that could:
 * - Break if layout changes
 * - Allow user code to run in kernel mode
 * - Create hard-to-track bugs
 * 
 * process_fork() exists only for POSIX syscall compatibility and uses spawn() internally.
 */
pid_t spawn(void (*entry)(void), const char *name, process_mode_t mode)
{
	process_t *proc;
	
	if (!entry || !name)
		return -1;

	serial_print("SERIAL: spawn: begin ");
	serial_print(name);
	serial_print("\n");
	
	proc = kmalloc_try(sizeof(process_t));
	if (!proc) {
		serial_print("[ERROR] Failed to allocate process structure\n");
		return -ENOMEM;
	}

	memset(proc, 0, sizeof(process_t));

	/* Basic process setup */
	proc->task.pid = process_get_next_pid();
	proc->ppid = current_process ? current_process->task.pid : 0;
	proc->state = PROCESS_READY;
	
	/* Explicit mode specification - no magic address detection */
	proc->mode = mode;
	proc->owns_page_directory = 1;

	/*
	 * Kernel idle reuses active kernel CR3 (boot/kmain tables).  Avoids
	 * remapping ~48MB of supervisor pages on every idle spawn (slow + PMM).
	 */
	if (mode == KERNEL_MODE && name && strcmp(name, "idle") == 0)
	{
		uint64_t kcr3 = get_current_page_directory();

		proc->page_directory = (uint64_t *)kcr3;
		proc->task.cr3 = kcr3;
		proc->owns_page_directory = 0;
		serial_print("SERIAL: spawn: kernel CR3 shared (idle)\n");
	}
	else
	{
		proc->page_directory = (uint64_t *)create_process_page_directory();
		if (!proc->page_directory)
		{
			serial_print("[ERROR] Failed to create page directory for process\n");
			kfree(proc);
			return -ENOMEM;
		}
		serial_print("SERIAL: spawn: page directory OK\n");
		proc->task.cr3 = (uint64_t)proc->page_directory;
	}

	/* Inherit permissions from current process or default to root */
	if (current_process) {
		proc->uid = current_process->uid;
		proc->gid = current_process->gid;
		proc->euid = current_process->euid;
		proc->egid = current_process->egid;
		proc->umask = current_process->umask;
		strncpy(proc->cwd, current_process->cwd, sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	} else {
		proc->uid = ROOT_UID;
		proc->gid = ROOT_GID;
		proc->euid = ROOT_UID;
		proc->egid = ROOT_GID;
		proc->umask = DEFAULT_UMASK;
		strncpy(proc->cwd, "/", sizeof(proc->cwd) - 1);
		proc->cwd[sizeof(proc->cwd) - 1] = '\0';
	}
	
	/* Set command name */
	strncpy(proc->comm, name, sizeof(proc->comm) - 1);
	proc->comm[sizeof(proc->comm) - 1] = '\0';

	/* Create user stack in userspace (only for USER_MODE processes) */
	if (proc->mode == USER_MODE)
	{
		/* User stack: [USER_STACK_TOP - USER_STACK_SIZE, USER_STACK_TOP) */
		proc->stack_size = USER_STACK_SIZE;
		proc->stack_start = USER_STACK_TOP - USER_STACK_SIZE;

		/*
		 * Map under kernel CR3: map_user_region_in_directory() allocates page
		 * tables from the kernel heap and must not run with child CR3 active.
		 */
		if (map_user_region_in_directory(proc->page_directory, proc->stack_start, proc->stack_size, PAGE_RW) != 0)
		{
			serial_print("SERIAL: spawn: stack map failed\n");
			process_unmap_user_pages_all(proc->page_directory, NULL);
			goto fail_proc;
		}
		serial_print("SERIAL: spawn: stack mapped\n");

		/* Setup stack pointer just below USER_STACK_TOP (stack grows down) */
		proc->task.rsp = USER_STACK_TOP - 16;
		proc->task.rbp = proc->task.rsp;
	}
	else
	{
		/* Kernel mode: allocate from kernel heap (existing behavior) */
		proc->stack_size = 0x2000;
		proc->stack_start = (uint64_t)kmalloc_try(proc->stack_size);
		if (!proc->stack_start)
		{
			if (proc->owns_page_directory)
				process_unmap_user_pages_all(proc->page_directory, NULL);
			goto fail_proc;
		}
		memset((void *)proc->stack_start, 0, proc->stack_size);
		proc->task.rsp = proc->stack_start + proc->stack_size - 16;
		proc->task.rbp = proc->task.rsp;
	}

	/* Setup task registers for clean start */
	proc->task.rip = (uint64_t)entry;
	if (proc->mode == USER_MODE)
		proc->task.rflags = ir0_rflags_sanitize_user(RFLAGS_IF);
	else
		proc->task.rflags = RFLAGS_IF;
	if (proc->mode == KERNEL_MODE)
	{
		proc->task.cs = KERNEL_CODE_SEL;
		proc->task.ss = KERNEL_DATA_SEL;
		proc->task.ds = KERNEL_DATA_SEL;
		proc->task.es = KERNEL_DATA_SEL;
		proc->task.fs = KERNEL_DATA_SEL;
		proc->task.gs = KERNEL_DATA_SEL;
	}
	else
	{
		proc->task.cs = USER_CODE_SEL;
		proc->task.ss = USER_DATA_SEL;
		proc->task.ds = USER_DATA_SEL;
		proc->task.es = USER_DATA_SEL;
		proc->task.fs = USER_DATA_SEL;
		proc->task.gs = USER_DATA_SEL;
	}

	/* Initialize file descriptor table */
	process_init_fd_table(proc);

	/* Initialize signal handlers to default */
	proc->signal_pending = 0;
	proc->signal_mask = 0;
	proc->signal_ignored = 0;
	proc->saved_context = NULL;
	for (int i = 0; i < _NSIG; i++)
	{
		proc->signal_handlers[i] = SIG_DFL;
	}

	/* Add to process list */
	{
		uint64_t irq_flags = process_irq_save();
		proc->next = process_list;
		process_list = proc;
		process_irq_restore(irq_flags);
	}

	fase43_proc_created++;
	process_fase43_proc_audit("spawn-after");
	proc->fase44_audit_state = FASE44_PROC_ALIVE;
	fase44_trace(proc->task.pid, "CREATED");
	fase44_ref_emit(proc, "spawn");
	process_fase44_list_checkpoint("spawn-after");

	/* Add to scheduler */
	sched_add_process(proc);

	return proc->task.pid;

fail_proc:
	if (proc->stack_start && proc->mode == KERNEL_MODE)
	{
		kfree((void *)proc->stack_start);
		proc->stack_start = 0;
	}
	if (proc->page_directory && proc->owns_page_directory)
	{
		kfree_aligned(proc->page_directory);
		proc->page_directory = NULL;
	}
	kfree(proc);
	return -ENOMEM;
}

/* Convenience wrapper for user-mode processes */
pid_t spawn_user(void (*entry)(void), const char *name)
{
	return spawn(entry, name, USER_MODE);
}

/* Convenience wrapper for kernel-mode processes */
pid_t spawn_kernel(void (*entry)(void), const char *name)
{
	return spawn(entry, name, KERNEL_MODE);
}

/*
 * validate_userspace_buffer - Same rules as syscalls.c for wait4 status pointers.
 */
static int validate_userspace_buffer(const void *buf, size_t size)
{
	if (!current_process)
		return -ESRCH;

	if (current_process->mode == KERNEL_MODE)
	{
		uint64_t addr = (uint64_t)buf;

		if (addr >= current_process->stack_start &&
		    addr + size <= current_process->stack_start + current_process->stack_size)
			return 0;
		if (current_process->heap_start > 0 &&
		    addr >= current_process->heap_start &&
		    addr + size <= current_process->heap_end)
			return 0;
		if (is_user_address(buf, size))
			return 0;
		return 0;
	}

	if (!is_user_address(buf, size))
		return -EFAULT;

	return 0;
}

static struct mmap_region *process_clone_mmap_list(struct mmap_region *parent_list)
{
	struct mmap_region *head = NULL;
	struct mmap_region *tail = NULL;
	struct mmap_region *walk;

	for (walk = parent_list; walk; walk = walk->next)
	{
		struct mmap_region *node = kmalloc_try(sizeof(*node));

		if (!node)
		{
			while (head)
			{
				struct mmap_region *next = head->next;

				kfree(head);
				head = next;
			}
			return NULL;
		}

		memcpy(node, walk, sizeof(*node));
		node->next = NULL;

		if (!head)
			head = node;
		else
			tail->next = node;
		tail = node;
	}

	return head;
}

#if defined(__x86_64__) || defined(__amd64__)
/*
 * process_capture_syscall_frame - Snapshot user GPRs at syscall dispatch entry.
 *
 * Must run before nested C calls (fork, wait4, execve path) clobber the
 * syscall kernel stack layout (Linux pt_regs at entry).
 */
/*
 * process_capture_syscall_frame_at_entry - Snapshot user frame at syscall insn entry.
 *
 * @frame_base: RSP at the arg6 stack slot (see syscall_insn_entry_64.asm).
 * Must run before syscall_dispatch C prologue runs.
 */
void process_capture_syscall_frame_at_entry(uint64_t *frame_base)
{
	process_t *p = current_process;
	syscall_user_frame_t *sf;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	sf->rip = frame_base[7];
	sf->rflags = frame_base[6];
	sf->rsp = frame_base[8];
	sf->rbx = frame_base[0];
	sf->rbp = frame_base[1];
	sf->r12 = frame_base[2];
	sf->r13 = frame_base[3];
	sf->r14 = frame_base[4];
	sf->r15 = frame_base[5];
	/* Linux ABI args sit below the callee-saved block on the syscall stack. */
	sf->rdi = frame_base[-1];
	sf->rsi = frame_base[-2];
	sf->rdx = frame_base[-3];
	sf->r10 = frame_base[-4];
	sf->r8 = frame_base[-5];
	sf->r9 = frame_base[-6];
}

void process_capture_syscall_frame(process_t *p)
{
	(void)p;
}

void process_apply_syscall_frame_to_task(task_t *task, const syscall_user_frame_t *sf,
                                         uint64_t rax)
{
	if (!task || !sf)
		return;

	task->rip = sf->rip;
	task->rsp = sf->rsp;
	task->rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task->rax = rax;
	task->rbx = sf->rbx;
	task->rbp = sf->rbp;
	task->r12 = sf->r12;
	task->r13 = sf->r13;
	task->r14 = sf->r14;
	task->r15 = sf->r15;
	task->rdi = sf->rdi;
	task->rsi = sf->rsi;
	task->rdx = sf->rdx;
	task->r10 = sf->r10;
	task->r8 = sf->r8;
	task->r9 = sf->r9;
	task->rcx = sf->rip;
	task->r11 = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	task->cs = USER_CODE_SEL;
	task->ss = USER_DATA_SEL;
	task->ds = USER_DATA_SEL;
	task->es = USER_DATA_SEL;
	task->fs = USER_DATA_SEL;
	task->gs = USER_DATA_SEL;
}

void process_arm_blocked_syscall_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->irq_frame_saved = 1;
}

/*
 * fork_ret - register-level fork child return audit (PRE_RETURN + FIRST_ENTRY).
 */
typedef struct
{
	uint64_t rax;
	uint64_t rcx;
	uint64_t r11;
	uint64_t rbx;
	uint64_t rbp;
	uint64_t r12;
	uint64_t r13;
	uint64_t r14;
	uint64_t r15;
	uint64_t rsp;
	uint64_t rip;
} fork_ret_pre_regs_t;

fork_ret_pre_regs_t fork_ret_pre_regs;

/*
 * fork_restore_audit — GPR restore path before iretq (ASM + C).
 * Layout must match switch_x64.asm offsets (FORK_RESTORE_AUDIT_*).
 */
typedef struct
{
	uint64_t magic;
	uint64_t task_ptr;
	uint64_t rax_slot_addr;
	uint64_t rax_slot_mem;
	uint64_t rsp_pre_gpr_load;
	uint64_t stack_qwords[20];
	uint64_t restore_method;
	uint64_t stack_rax_slot_off;
	uint64_t live_rax_after_task_load;
	uint64_t live_rbx_after_task_load;
	uint64_t live_rax_after_pr_call;
	uint64_t live_rax_pre_iretq;
	uint64_t live_rbx_pre_iretq;
	uint64_t live_rcx_pre_iretq;
	uint64_t live_rdx_pre_iretq;
	uint64_t kernel_rsp_pre_iretq;
	uint64_t iretq_rip;
	uint64_t iretq_rflags;
	uint64_t iretq_user_rsp;
	uint64_t task_rax_at_fixup;
	uint64_t pre_return_log_rax;
	uint64_t userspace_rax;
	uint64_t classify_emit;
} fork_restore_audit_t;

fork_restore_audit_t fork_restore_audit;

extern uint8_t fork_flow_set_tf;

static void fork_restore_dump_qwords(const char *tag, uint64_t base, size_t n)
{
	size_t i;

	serial_print("[FORK_RESTORE] ");
	serial_print(tag ? tag : "frame");
	serial_print(" base=");
	serial_print_hex64(base);
	serial_print(" qwords=");
	for (i = 0; i < n; i++)
	{
		uint64_t addr = base + (uint64_t)(i * sizeof(uint64_t));
		uint64_t v = *(const uint64_t *)(uintptr_t)addr;

		if (i > 0)
			serial_print(" ");
		serial_print_hex64(v);
	}
	serial_print("\n");
}

static void fork_restore_classify(uint64_t userspace_rax)
{
	const char *tag = NULL;
	uint64_t slot;
	uint64_t after_load;
	uint64_t pre_iret;
	uint64_t pre_log;
	uint64_t fixup_rax;

	if (fork_restore_audit.classify_emit)
		return;

	fork_restore_audit.classify_emit = 1;
	fork_restore_audit.userspace_rax = userspace_rax;
	slot = fork_restore_audit.rax_slot_mem;
	after_load = fork_restore_audit.live_rax_after_task_load;
	pre_iret = fork_restore_audit.live_rax_pre_iretq;
	pre_log = fork_restore_audit.pre_return_log_rax;
	fixup_rax = fork_restore_audit.task_rax_at_fixup;

	if (fixup_rax == 0 && slot != 0)
		tag = "RAX_SLOT_STALE";
	else if (after_load != slot)
		tag = "RESTORE_SOURCE_MISMATCH";
	else if (pre_log != after_load)
		tag = "PRE_RETURN_LOG_NOT_AUTHORITATIVE";
	else if (after_load == 0 && pre_iret != 0)
		tag = "LATE_RAX_CLOBBER";
	else if (after_load == 0 && pre_iret == 0 && fixup_rax == 0)
		tag = "LATE_RAX_CLOBBER_FIXED";

	if (!tag)
		return;

	serial_print("[FORK_RESTORE][CLASSIFY] ");
	serial_print(tag);
	serial_print("\n");
}

static void fork_restore_log_fixup(process_t *parent, process_t *child)
{
	uint64_t rax_before;

	rax_before = child->task.rax;
	fork_restore_audit.magic = 0xF010CAFEULL;
	fork_restore_audit.task_ptr = (uint64_t)(uintptr_t)&child->task;
	fork_restore_audit.rax_slot_addr =
		(uint64_t)(uintptr_t)&child->task.rax;

	serial_print("[FORK_RESTORE][FIXUP] child_task=");
	serial_print_hex64(fork_restore_audit.task_ptr);
	serial_print(" syscall_frame=");
	serial_print_hex64((uint64_t)(uintptr_t)&parent->syscall_frame);
	serial_print(" rax_slot_addr=");
	serial_print_hex64(fork_restore_audit.rax_slot_addr);
	serial_print(" task_rax_before=");
	serial_print_hex64(rax_before);

	process_apply_syscall_frame_to_task(&child->task, &parent->syscall_frame, 0);

	fork_restore_audit.task_rax_at_fixup = child->task.rax;
	fork_restore_audit.rax_slot_mem = child->task.rax;

	serial_print(" task_rax_after=");
	serial_print_hex64(child->task.rax);
	serial_print(" slot_readback=");
	serial_print_hex64(*(uint64_t *)(uintptr_t)fork_restore_audit.rax_slot_addr);
	serial_print("\n");
}

#define FORK_BRANCH_RIP_MOV   0x402BA3ULL
#define FORK_BRANCH_RIP_TEST  0x402BA6ULL
#define FORK_BRANCH_RIP_JE    0x402BA8ULL
#define FORK_BRANCH_RIP_CHILD 0x402BE8ULL
#define FORK_BRANCH_RIP_PARENT 0x402BAAULL

static void fork_flow_hex_byte(uint8_t b)
{
	static const char *digits = "0123456789abcdef";
	char out[3];

	out[0] = digits[(b >> 4) & 0x0FU];
	out[1] = digits[b & 0x0FU];
	out[2] = '\0';
	serial_print(out);
}

static int fork_flow_read_user_bytes(process_t *proc, uint64_t va, uint8_t *buf,
                                     size_t n)
{
	size_t i;

	if (!proc || !proc->page_directory || !buf || n == 0)
		return -1;

	for (i = 0; i < n; i++)
	{
		uintptr_t page = (uintptr_t)((va + i) & ~0xFFFULL);
		size_t off = (size_t)((va + i) & 0xFFFULL);
		uint64_t *pte = paging_get_pte(proc->page_directory, page);
		uintptr_t phys;

		if (!pte || !(*pte & PAGE_PRESENT))
			return -1;

		phys = (uintptr_t)(*pte & PAGE_PTE_PFN_MASK);
		buf[i] = *(const uint8_t *)(phys + off);
	}

	return 0;
}

static struct
{
	uint8_t active;
	uint8_t pre_return_done;
	pid_t child_pid;
	uint64_t expected_rax;
	uint64_t expected_rip;
	uint64_t expected_rsp;
	uint64_t task_rax_at_fixup;
	uint64_t pre_rax;
	uint64_t pre_rip;
	uint64_t pre_rsp;
} fork_ret_expect;

static struct
{
	uint8_t active;
	uint8_t step;
	uint8_t classified;
	uint8_t code_dumped;
	pid_t child_pid;
	uint64_t step_rax[3];
	uint64_t step_rbx[3];
	uint64_t step_rip[3];
	uint64_t step_rflags[3];
	uint64_t step_rsp[3];
	uint64_t step_zf[3];
} fork_branch;

static void fork_branch_hex8(process_t *proc, uint64_t va, const char *tag)
{
	uint8_t bytes[8];
	size_t i;

	if (fork_flow_read_user_bytes(proc, va, bytes, sizeof(bytes)) != 0)
	{
		serial_print("[FORK_BRANCH] ");
		serial_print(tag ? tag : "code");
		serial_print(" unreadable\n");
		return;
	}

	serial_print("[FORK_BRANCH] ");
	serial_print(tag ? tag : "code");
	serial_print("=");
	for (i = 0; i < sizeof(bytes); i++)
	{
		if (i > 0)
			serial_print(" ");
		fork_flow_hex_byte(bytes[i]);
	}
	serial_print("\n");
}

static void fork_branch_emit_step(process_t *proc, const char *label, uint32_t idx,
                                  uint64_t rip, uint64_t rax, uint64_t rbx,
                                  uint64_t rflags, uint64_t rsp)
{
	uint64_t zf;

	zf = (rflags >> 6) & 1ULL;
	serial_print("[FORK_BRANCH] step=");
	serial_print(label ? label : "?");
	serial_print(" rip=");
	serial_print_hex64(rip);
	serial_print(" rax=");
	serial_print_hex64(rax);
	serial_print(" rbx=");
	serial_print_hex64(rbx);
	serial_print(" rflags=");
	serial_print_hex64(rflags);
	serial_print(" zf=");
	serial_print_hex64(zf);
	serial_print(" rsp=");
	serial_print_hex64(rsp);
	serial_print("\n");

	if (idx < 3)
	{
		fork_branch.step_rax[idx] = rax;
		fork_branch.step_rbx[idx] = rbx;
		fork_branch.step_rip[idx] = rip;
		fork_branch.step_rflags[idx] = rflags;
		fork_branch.step_rsp[idx] = rsp;
		fork_branch.step_zf[idx] = zf;
	}

	if (idx == 0 && fork_ret_expect.active)
	{
		fork_restore_emit_pre_iretq();
		fork_restore_classify(rax);
	}

	if (!fork_branch.code_dumped && proc)
	{
		fork_branch_hex8(proc, FORK_BRANCH_RIP_MOV, "code_at_402BA3");
		fork_branch_hex8(proc, FORK_BRANCH_RIP_TEST, "code_at_402BA6");
		fork_branch_hex8(proc, FORK_BRANCH_RIP_JE, "code_at_402BA8");
		fork_branch.code_dumped = 1;
	}
}

static void fork_branch_classify(void)
{
	const char *tag;
	uint64_t rax0;
	uint64_t rax1;
	uint64_t zf1;
	uint64_t rip2;

	if (fork_branch.classified)
		return;

	fork_branch.classified = 1;
	fork_flow_set_tf = 0;
	rax0 = fork_branch.step_rax[0];
	rax1 = fork_branch.step_rax[1];
	zf1 = fork_branch.step_zf[1];
	rip2 = fork_branch.step_rip[2];

	if (rax0 != rax1)
		tag = "RAX_MUTATED_BETWEEN_STEPS";
	else if (((rax1 == 0) ? 1ULL : 0ULL) != zf1)
		tag = "FLAGS_UNEXPECTED";
	else if (rax1 == 0 && rip2 != FORK_BRANCH_RIP_CHILD)
		tag = "BRANCH_PARENT_WITH_RAX_ZERO";
	else if (rip2 == FORK_BRANCH_RIP_CHILD)
		tag = "BRANCH_CHILD_OK";
	else
		return;

	serial_print("[FORK_BRANCH][CLASSIFY] ");
	serial_print(tag);
	serial_print("\n");
}

static void fork_branch_arm_pid(pid_t child_pid)
{
	memset(&fork_branch, 0, sizeof(fork_branch));
	fork_branch.active = 1;
	fork_branch.child_pid = child_pid;
}

int fork_flow_note_debug_exception(uint64_t *stack)
{
	process_t *p = current_process;
	uint64_t rip;
	uint64_t rax;
	uint64_t rbx;
	uint64_t rflags;
	uint64_t rsp;
	const char *label;

	if (!fork_branch.active || fork_branch.classified || !stack || !p ||
	    p->task.pid != fork_branch.child_pid)
		return 0;

	rip = stack[2];
	rflags = stack[4];
	rsp = stack[5];
	rax = stack[-1];
	rbx = stack[-4];

	if (fork_branch.step == 0)
		label = "STEP0";
	else if (fork_branch.step == 1)
		label = "STEP1";
	else if (fork_branch.step == 2)
		label = "STEP2";
	else
	{
		stack[4] &= ~0x100ULL;
		fork_flow_set_tf = 0;
		return 0;
	}

	fork_branch_emit_step(p, label, fork_branch.step, rip, rax, rbx, rflags, rsp);
	fork_branch.step++;

	if (fork_branch.step >= 3)
	{
		stack[4] &= ~0x100ULL;
		fork_branch_classify();
	}
	else
		stack[4] |= 0x100ULL;

	return 1;
}

void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall)
{
	(void)rip_hw;
	(void)nr;
	(void)from_syscall;
}

void fork_ret_emit_pre_return(void)
{
	process_t *p = current_process;
	const fork_ret_pre_regs_t *pre = &fork_ret_pre_regs;

	if (!fork_ret_expect.active || fork_ret_expect.pre_return_done)
		return;
	if (!p || p->task.pid != fork_ret_expect.child_pid)
		return;

	fork_ret_expect.pre_return_done = 1;
	fork_ret_expect.pre_rax = pre->rax;
	fork_ret_expect.pre_rip = pre->rip;
	fork_ret_expect.pre_rsp = pre->rsp;
	if (ir0_debug_fork_singlestep_active())
		fork_flow_set_tf = 1;
	fork_restore_audit.pre_return_log_rax = pre->rax;

	serial_print("[FORK_RET][PRE_RETURN] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" task_ptr=");
	serial_print_hex64((uint64_t)(uintptr_t)&p->task);
	serial_print(" rax_slot_addr=");
	serial_print_hex64((uint64_t)(uintptr_t)&p->task.rax);
	serial_print(" rax_slot_val=");
	serial_print_hex64(p->task.rax);
	serial_print(" restore_method=");
	serial_print_hex64(fork_restore_audit.restore_method);
	serial_print(" rax=");
	serial_print_hex64(pre->rax);
	serial_print(" live_after_task_load=");
	serial_print_hex64(fork_restore_audit.live_rax_after_task_load);
	serial_print(" rcx=");
	serial_print_hex64(pre->rcx);
	serial_print(" r11=");
	serial_print_hex64(pre->r11);
	serial_print(" rbx=");
	serial_print_hex64(pre->rbx);
	serial_print(" rbp=");
	serial_print_hex64(pre->rbp);
	serial_print(" r12=");
	serial_print_hex64(pre->r12);
	serial_print(" r13=");
	serial_print_hex64(pre->r13);
	serial_print(" r14=");
	serial_print_hex64(pre->r14);
	serial_print(" r15=");
	serial_print_hex64(pre->r15);
	serial_print(" rsp=");
	serial_print_hex64(pre->rsp);
	serial_print(" rip=");
	serial_print_hex64(pre->rip);
	serial_print(" task_rax=");
	serial_print_hex64(p->task.rax);
	serial_print("\n");

	fork_restore_dump_qwords("stack_at_pre_return", pre->rsp, 20);
	fork_restore_dump_qwords("asm_stack_pre_gpr", fork_restore_audit.rsp_pre_gpr_load, 20);
	serial_print("[FORK_RESTORE] asm_rax_slot_mem=");
	serial_print_hex64(fork_restore_audit.rax_slot_mem);
	serial_print(" stack_rax_off=");
	serial_print_hex64(fork_restore_audit.stack_rax_slot_off);
	serial_print(" live_after_pr_call=");
	serial_print_hex64(fork_restore_audit.live_rax_after_pr_call);
	serial_print("\n");
}

void fork_restore_emit_pre_iretq(void)
{
	if (!fork_ret_expect.active || !fork_ret_expect.pre_return_done)
		return;

	serial_print("[FORK_RESTORE][PRE_IRETQ] live_rax=");
	serial_print_hex64(fork_restore_audit.live_rax_pre_iretq);
	serial_print(" live_rbx=");
	serial_print_hex64(fork_restore_audit.live_rbx_pre_iretq);
	serial_print(" live_rcx=");
	serial_print_hex64(fork_restore_audit.live_rcx_pre_iretq);
	serial_print(" live_rdx=");
	serial_print_hex64(fork_restore_audit.live_rdx_pre_iretq);
	serial_print(" kernel_rsp=");
	serial_print_hex64(fork_restore_audit.kernel_rsp_pre_iretq);
	serial_print(" iretq_rip=");
	serial_print_hex64(fork_restore_audit.iretq_rip);
	serial_print(" iretq_rflags=");
	serial_print_hex64(fork_restore_audit.iretq_rflags);
	serial_print(" iretq_user_rsp=");
	serial_print_hex64(fork_restore_audit.iretq_user_rsp);
	serial_print("\n");
}

void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw)
{
	process_t *p = current_process;

	if (!fork_ret_expect.active || !p || p->task.pid != fork_ret_expect.child_pid)
		return;
	if (!fork_ret_expect.pre_return_done)
		return;

	if (!fork_branch.classified)
	{
		serial_print("[FORK_RET][FIRST_ENTRY] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" rax=");
		serial_print_hex64(rax_hw);
		serial_print(" rip=");
		serial_print_hex64(rip_hw);
		serial_print(" rsp=");
		serial_print_hex64(rsp_hw);
		serial_print("\n");
	}
}

static void fork_ret_arm(process_t *child)
{
	memset(&fork_ret_expect, 0, sizeof(fork_ret_expect));
	fork_ret_expect.active = 1;
	fork_ret_expect.child_pid = child->task.pid;
	fork_ret_expect.expected_rax = child->task.rax;
	fork_ret_expect.expected_rip = child->task.rip;
	fork_ret_expect.expected_rsp = child->task.rsp;
	fork_ret_expect.task_rax_at_fixup = child->task.rax;
	fork_branch_arm_pid(child->task.pid);
}

static void fork_fixup_user_syscall_return(process_t *parent, process_t *child)
{
	memset(&fork_restore_audit, 0, sizeof(fork_restore_audit));
	fork_restore_log_fixup(parent, child);
	fork_ret_arm(child);
}
#endif /* __x86_64__ */

#if !(defined(__x86_64__) || defined(__amd64__))
void fork_ret_emit_pre_return(void)
{
}

void fork_restore_emit_pre_iretq(void)
{
}

void fork_ret_first_syscall_entry(uint64_t rax_hw, uint64_t rip_hw, uint64_t rsp_hw)
{
	(void)rax_hw;
	(void)rip_hw;
	(void)rsp_hw;
}

int fork_flow_note_debug_exception(uint64_t *stack)
{
	(void)stack;
	return 0;
}

void fork_flow_note_kernel_entry(uint64_t rip_hw, uint64_t nr, int from_syscall)
{
	(void)rip_hw;
	(void)nr;
	(void)from_syscall;
}
#endif

static void fork_destroy_child_mm(process_t *child)
{
	if (!child || !child->page_directory || !child->owns_page_directory)
		return;

	process_unmap_user_pages_all(child->page_directory, NULL);
	paging_reclaim_lower_half_tables(child->page_directory);
	paging_fase42_note_pml4_freed((uint64_t)(uintptr_t)child->page_directory);
	kfree_aligned(child->page_directory);
	child->page_directory = NULL;
	child->owns_page_directory = 0;
	process_fase43_note_mm_destroyed();
}

static void fork_free_mmap_list(process_t *child)
{
	struct mmap_region *r;
	struct mmap_region *next;

	if (!child)
		return;

	r = child->mmap_list;
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	child->mmap_list = NULL;
}

static process_t *fork_process_create(process_t *parent, pid_t *child_pid_out)
{
	process_t *child;
	pid_t child_pid;

	if (!parent || !child_pid_out)
		return NULL;

	child = kmalloc_try(sizeof(process_t));
	if (!child)
		return NULL;

	memcpy(child, parent, sizeof(process_t));

	child_pid = process_get_next_pid();
	child->task.pid = child_pid;
	child->ppid = parent->task.pid;
	child->state = PROCESS_READY;
	child->next = NULL;
	child->saved_context = NULL;
	child->poll_waiter = NULL;
	child->irq_frame_saved = 0;
	child->wait_status_ptr = NULL;
	child->syscall_resume_rax = 0;
	child->fase44_audit_state = FASE44_PROC_ALIVE;
	child->page_directory = NULL;
	child->owns_page_directory = 0;
	child->mmap_list = NULL;
	child->fase46_fork_generation = parent->fase46_fork_generation + 1U;
	child->fase46_fork_parent_pid = parent->task.pid;
	child->fase46_entered_userspace = 0;
	child->fase46_entered_exit = 0;
	child->fase46_entered_wait = 0;
	memset(child->fd_table, 0, sizeof(child->fd_table));

	*child_pid_out = child_pid;
	fase45_fork_state(child_pid, "CREATED");
	return child;
}

static int fork_child_mm_create(process_t *child)
{
	if (!child)
		return -ENOMEM;

	child->page_directory = (uint64_t *)create_process_page_directory();
	if (!child->page_directory)
	{
		fase45_fork_state(child->task.pid, "FAILED");
		return -ENOMEM;
	}

	child->owns_page_directory = 1;
	child->task.cr3 = (uint64_t)(uintptr_t)child->page_directory;
	fase45_fork_state(child->task.pid, "MM_CREATED");
	return 0;
}

static int duplicate_fd_table(process_t *parent, process_t *child)
{
	int i;

	if (!parent || !child)
		return -EINVAL;

	memcpy(child->fd_table, parent->fd_table, sizeof(child->fd_table));
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		fd_entry_t *e = &child->fd_table[i];

		if (!e->in_use)
			continue;
		if (e->is_pipe && e->vfs_file)
			pipe_acquire_end((pipe_t *)e->vfs_file, e->pipe_end);
		else if (e->vfs_file)
			vfs_file_acquire((struct vfs_file *)e->vfs_file);
	}
	fase45_fork_state(child->task.pid, "FILES_CLONED");
	return 0;
}

static int fork_enqueue_process(process_t *child)
{
	uint64_t irq_flags;

	if (!child)
		return -EINVAL;

	irq_flags = process_irq_save();
	child->next = process_list;
	process_list = child;
	process_irq_restore(irq_flags);

	sched_add_process(child);
	fase45_fork_state(child->task.pid, "SCHEDULED");
	fase46_scheduled++;
	return 0;
}

static void fork_rollback(process_t *child, pid_t child_pid, int enqueued)
{
	if (!child)
		return;

	fase45_fork_state(child_pid, "FAILED");
	fase45_fork_state(child_pid, "ROLLBACK");
	fase45_fork_rollback++;

	if (enqueued)
	{
		sched_remove_process(child);
		(void)process_remove_from_list(child);
	}

	fase45_assert_child_not_visible(child_pid);

	fork_destroy_child_mm(child);
	fork_free_mmap_list(child);

	fase45_fork_state(child_pid, "DESTROYED");
	kfree(child);

	fase45_assert_child_not_visible(child_pid);
	process_fase45_fork_audit("rollback");
}

/*
 * fork() - POSIX fork for user and kernel processes.
 *
 * Clones the parent process struct and user address space (full copy, no COW),
 * duplicates the FD table, and arranges for the child to return 0 from fork().
 */
pid_t fork(void)
{
	process_t *parent = current_process;
	process_t *child;
	pid_t child_pid;

	if (!parent)
		return -1;
	if (!fase46_frames_baseline_set && parent->task.pid == 1)
	{
		size_t total_frames = 0;
		size_t used_frames = 0;

		pmm_stats(&total_frames, &used_frames, NULL);
		fase46_frames_baseline = (uint64_t)used_frames;
		fase46_frames_baseline_set = 1;
	}
	process_fase43_proc_audit("fork-before");
	process_fase44_list_checkpoint("fork-before");
	process_fase45_fork_audit("fork-before");
	paging_fase42_checkpoint("fork-before", (int32_t)parent->task.pid);

	child = fork_process_create(parent, &child_pid);
	if (!child)
		return -ENOMEM;

	if (fork_child_mm_create(child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (copy_process_memory(parent, child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}
	fase45_fork_state(child_pid, "MEMORY_CLONED");

	child->mmap_list = process_clone_mmap_list(parent->mmap_list);
	if (parent->mmap_list && !child->mmap_list)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	if (duplicate_fd_table(parent, child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	child->task.rax = 0;
	child->task.cr3 = (uint64_t)(uintptr_t)child->page_directory;
	child->task.pid = child_pid;

#if defined(__x86_64__) || defined(__amd64__)
	if (parent->mode == USER_MODE)
	{
		fork_fixup_user_syscall_return(parent, child);
		process_fase46_proc_log(parent, (int64_t)child_pid, "AFTER_FORK");
		process_fase46_proc_log(child, 0, "USER_ENTER");
	}
#endif

	if (fork_enqueue_process(child) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	fase43_proc_created++;
	process_fase43_proc_audit("fork-after");
	fase44_trace(child_pid, "CREATED");
	fase44_ref_emit(child, "fork");
	process_fase44_list_checkpoint("fork-after");
	process_fase45_fork_audit("fork-after");

	paging_fase42_checkpoint("fork-after", (int32_t)child_pid);

	return child_pid;
}

/* Find process by PID */
process_t *process_find_by_pid(pid_t pid)
{
	process_t *proc;
	uint64_t irq_flags = process_irq_save();

	proc = process_list;
	
	while (proc)
	{
		if (proc->task.pid == pid)
		{
			process_irq_restore(irq_flags);
			return proc;
		}
		proc = proc->next;
	}
	
	process_irq_restore(irq_flags);
	return NULL; /* Not found */
}

int process_remove_from_list(process_t *target)
{
	process_t *scan;
	process_t *prev;
	uint64_t irq_flags;

	if (!target)
		return -EINVAL;

	irq_flags = process_irq_save();
	prev = NULL;
	scan = process_list;

	while (scan)
	{
		if (scan == target)
		{
			if (prev)
				prev->next = scan->next;
			else
				process_list = scan->next;
			scan->next = NULL;
			process_irq_restore(irq_flags);
			return 0;
		}
		prev = scan;
		scan = scan->next;
	}

	process_irq_restore(irq_flags);
	return -ENOENT;
}

/**
 * process_reparent_children - Reparent all children to init (PID 1)
 * @dying_parent: Process that is about to exit
 *
 * When a parent process dies, all its children become orphans.
 * This function reparents them to init (PID 1) so they don't become zombies.
 */
static void process_reparent_children(process_t *dying_parent)
{
	process_t *child;
	process_t *init;
	
	if (!dying_parent)
		return;
	
	/* Find init process (PID 1) */
	init = process_find_by_pid(1);
	if (!init)
	{
		/* No init process - this is a critical system error */
		serial_print("[CRITICAL] Init process (PID 1) not found during reparenting\n");
		serial_print("[CRITICAL] System integrity compromised - orphaned processes detected\n");
		/* Continue execution but log the critical error */
		return;
	}
	
	/* Find all children of dying parent */
	child = process_list;
	while (child)
	{
		if (child->ppid == dying_parent->task.pid)
		{
			child->ppid = 1;
			fase43_reparent_events++;
			fase44_destroy_audit(child, dying_parent->task.pid,
					     child->fase44_audit_state,
					     child->fase44_audit_state, 0,
					     "reparent");
#if DEBUG_PROCESS
			serial_print("[PROCESS] Reparented child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" to init (PID 1)\n");
#endif
		}
		child = child->next;
	}
	process_fase44_list_checkpoint("reparent-after");
}

/**
 * process_reap_zombies - Automatically reap zombie children of a process
 * @parent: Parent process
 *
 * When a process exits, clean up any zombie children that were waiting for it.
 * Also used by init to periodically clean up zombies.
 */
void process_reap_zombies(process_t *parent)
{
	process_t *child;
	process_t *next;
	
	if (!parent)
		return;
	
	child = process_list;
	
	while (child)
	{
		next = child->next;
		
		/* Check if this is a zombie child of the parent */
		if (child->ppid == parent->task.pid && child->state == PROCESS_ZOMBIE)
		{
#if DEBUG_PROCESS
			serial_print("[PROCESS] Auto-reaping zombie child PID ");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print("\n");
#endif
			fase43_reap_events++;
			process_fase43_proc_audit("reap-zombie");
			fase44_reap_zombie(child, parent->task.pid, "reap-zombie");
		}
		
		child = next;
	}
	process_fase44_list_checkpoint("reap-zombie-after");
}

static void process_wait_wake_blocked_parent(process_t *parent, process_t *child)
{
	int status_val;
	int *status_ptr;
	int copy_ret;

	if (!parent || !child || !parent->irq_frame_saved)
		return;

	status_val = (child->exit_code & 0xFF) << 8;
	status_ptr = parent->wait_status_ptr;
	if (!status_ptr)
		status_ptr = (int *)(uintptr_t)parent->syscall_frame.rsi;

	copy_ret = -1;
	if (status_ptr && parent->page_directory &&
	    validate_userspace_buffer(status_ptr, sizeof(int)) == 0)
	{
		copy_ret = copy_to_user_region_in_directory(parent->page_directory,
							    (uintptr_t)status_ptr,
							    &status_val,
							    sizeof(int));
	}

	fase51_dbg_wait_wake((uint32_t)parent->task.pid, (uint32_t)child->task.pid,
			     status_ptr, status_val, copy_ret);
	parent->syscall_resume_rax = (uint64_t)child->task.pid;
	parent->state = PROCESS_READY;
}

void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid)
{
	process_t *child;
	size_t used_frames_before = 0;
	size_t used_frames_after = 0;

	if (!parent || child_pid <= 0)
		return;

	child = process_find_by_pid(child_pid);
	if (!child || child->state != PROCESS_ZOMBIE ||
	    child->ppid != parent->task.pid)
		return;

	pmm_stats(NULL, &used_frames_before, NULL);
	process_fase43_proc_audit("wait-resume-before-reap");
	fase44_reap_zombie(child, parent->task.pid, "wait-resume");
	pmm_stats(NULL, &used_frames_after, NULL);
	process_fase44_list_checkpoint("wait-resume-after");
	process_fase43_proc_audit("wait-resume-after-reap");
	serial_print("[FASE41][WAIT_REAP] parent_pid=");
	serial_print_hex32((uint32_t)parent->task.pid);
	serial_print(" child_pid=");
	serial_print_hex32((uint32_t)child_pid);
	serial_print(" used_before=");
	serial_print_hex64((uint64_t)used_frames_before);
	serial_print(" used_after=");
	serial_print_hex64((uint64_t)used_frames_after);
	serial_print(" delta=");
	if (used_frames_after >= used_frames_before)
		serial_print_hex64((uint64_t)(used_frames_after - used_frames_before));
	else
		serial_print_hex64((uint64_t)(used_frames_before - used_frames_after));
	serial_print(" sign=");
	serial_print(used_frames_after <= used_frames_before ? "-" : "+");
	serial_print("\n");
	serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] ");
	serial_print(used_frames_after <= used_frames_before ?
		     "PMM_RECLAIM_ON_WAIT_OK" : "PMM_RECLAIM_ON_WAIT_PARTIAL");
	serial_print("\n");
	paging_fase42_checkpoint("wait-resume-after", (int32_t)parent->task.pid);
}

__attribute__((noreturn)) void process_exit(int code)
{
	process_t *dying = current_process;
	process_t *parent;
	size_t total_frames = 0;
	size_t used_frames = 0;
	uint64_t vmas = 0;

	if (!dying)
	{
		for (;;)
			arch_cpu_idle();
	}
	fase50_trace_proc("process_exit-entry", dying);
	dying->irq_frame_saved = 0;
	serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] ZOMBIE_IRQ_SAVED_CLEARED\n");

	/* Before becoming a zombie:
	 * 1. Reparent all children to init (PID 1) to avoid orphaned processes
	 * 2. Clean up any zombie children we were waiting for
	 */
	process_reap_zombies(dying);
	process_reparent_children(dying);

	process_release_fds(dying, "EXIT_CLOSE");

	process_fase46_proc_log(dying, (int64_t)(uint32_t)code, "EXIT");
	process_fase44_list_checkpoint("exit-before");
	dying->fase44_audit_state = FASE44_PROC_EXITING;
	fase44_trace(dying->task.pid, "EXIT");
	fase44_ref_emit(dying, "exit");
	process_fase43_proc_audit("exit-before");

	/* Mark as zombie */
	dying->state = PROCESS_ZOMBIE;
	dying->exit_code = code;
	dying->fase44_audit_state = FASE44_PROC_ZOMBIE;
	fase44_trace(dying->task.pid, "ZOMBIE");
	fase43_proc_exited++;
	fase43_proc_zombie++;
	paging_fase42_checkpoint("exit-before", (int32_t)dying->task.pid);
	for (struct mmap_region *r = dying->mmap_list; r; r = r->next)
		vmas++;
	pmm_stats(&total_frames, &used_frames, NULL);
	serial_print("[FASE41][EXIT] pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" vmas=");
	serial_print_hex64(vmas);
	serial_print(" used_frames=");
	serial_print_hex64((uint64_t)used_frames);
	serial_print(" total_frames=");
	serial_print_hex64((uint64_t)total_frames);
	serial_print("\n");
	serial_print("[PROCESS] exit pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" code=");
	serial_print_hex64((uint64_t)(uint32_t)code);
	serial_print("\n");

	process_fase43_proc_audit("exit-after");
	if (dying->task.pid == 1)
	{
		process_fase44_drain_zombie_children(1);
		process_fase43_live_proc_dump();
		process_fase44_live_summary("init-exit");
	}
	process_fase44_list_checkpoint("exit-after");

	/* Send SIGCHLD to parent process if it exists */
	if (dying->ppid > 0)
	{
		int parent_state_before = -1;

		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED)
				process_wait_wake_blocked_parent(parent, dying);
		}
		else if (!parent || parent->state == PROCESS_ZOMBIE)
		{
			/* Parent is dead or zombie - reparent to init and send SIGCHLD to init */
			dying->ppid = 1;
			parent = process_find_by_pid(1);
			if (parent)
			{
				if (parent_state_before == -1)
					parent_state_before = parent->state;
				send_signal(parent->task.pid, SIGCHLD);
				if (parent->state == PROCESS_BLOCKED)
					process_wait_wake_blocked_parent(parent, dying);
			}
		}
		wait_exit_audit_process_exit(dying, parent, parent_state_before);
	}
	else
	{
		wait_exit_audit_process_exit(dying, NULL, -1);
	}

	/* Remove process from scheduler - it should no longer be scheduled.
	 * The process structure remains in memory as a zombie until reaped
	 * by the parent (via wait()), but it will not consume CPU time.
	 */
	sched_remove_process(dying);
	fase50_trace_proc("process_exit-before-schedule", dying);

	/*
	 * kmain keeps the kernel idle task off the RR queue while PID 1 runs;
	 * enqueue it again when a user process exits so sched has a fallback.
	 */
	{
		process_t *p;

		for (p = process_list; p; p = p->next)
		{
			if (p->mode == KERNEL_MODE && p->state != PROCESS_ZOMBIE &&
			    strncmp(p->comm, "idle", sizeof(p->comm)) == 0)
			{
				sched_add_process(p);
				break;
			}
		}
	}

	/* Switch to another process - this will never return to this code.
	 * The zombie process remains in memory with its exit code for the
	 * parent to retrieve via wait().
	 */
	sched_schedule_next();

	/* No runnable task: halt forever (must not sysret to exited user context). */
	for (;;)
		arch_cpu_idle();
}


/*
 * process_destroy - Release per-process resources before freeing a zombie struct.
 * Closes VFS and pipe handles, clears the FD table, and tears down user mappings
 * in this process's page directory (not necessarily the active CR3).
 */
void process_destroy(process_t *p)
{
	struct mmap_region *r;
	struct mmap_region *next;
	process_reclaim_stats_t reclaim_stats;
	uint64_t orphan_frames = 0;
	uint64_t double_free = 0;
	uint64_t alive_owner_missing = 0;

	if (!p)
		return;
	memset(&reclaim_stats, 0, sizeof(reclaim_stats));

	ir0_console_purge_waiters_for_process(p);

	process_fase46_proc_log(p, -1, "DESTROY");
	fase44_ref_emit(p, "destroy");
	fase43_proc_destroyed++;
	process_fase43_proc_audit("destroy-before");

	serial_print("[PROCESS] destroy PID ");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" (fd cleanup)\n");

	process_release_fds(p, "DESTROY");

	/* Unmap all user pages in this process's PML4 (reaper may run under another CR3) */
	if (p->page_directory && p->owns_page_directory)
		process_unmap_user_pages_all(p->page_directory, &reclaim_stats);
	if (p->page_directory && p->owns_page_directory)
		paging_reclaim_lower_half_tables(p->page_directory);

	/* Drop mmap bookkeeping nodes associated with this process. */
	r = p->mmap_list;
	while (r)
	{
		next = r->next;
		kfree(r);
		r = next;
	}
	p->mmap_list = NULL;

	if (p->saved_context)
	{
		kfree(p->saved_context);
		p->saved_context = NULL;
	}

	if (p->mode == KERNEL_MODE && p->stack_start &&
	    p->stack_start != INIT_DEBUG_STACK_BASE)
	{
		kfree((void *)p->stack_start);
		p->stack_start = 0;
		p->stack_size = 0;
	}

	if (p->page_directory && p->owns_page_directory)
	{
		paging_fase42_note_pml4_freed((uint64_t)(uintptr_t)p->page_directory);
		kfree_aligned(p->page_directory);
		p->page_directory = NULL;
		process_fase43_note_mm_destroyed();
	}

	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	serial_print("[FASE41][RECLAIM] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" pages_owned=");
	serial_print_hex64(reclaim_stats.mapped_pages);
	serial_print(" pages_freed=");
	serial_print_hex64(reclaim_stats.freed_pages);
	serial_print(" missing_pages=");
	serial_print_hex64(reclaim_stats.missing_pages);
	serial_print(" delta=");
	serial_print_hex64(reclaim_stats.mapped_pages - reclaim_stats.freed_pages);
	serial_print("\n");
	serial_print("[FASE41][PT] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" pdpt_present=");
	serial_print_hex64(reclaim_stats.pdpt_present);
	serial_print(" pd_present=");
	serial_print_hex64(reclaim_stats.pd_present);
	serial_print(" pt_present=");
	serial_print_hex64(reclaim_stats.pt_present);
	serial_print(" leaf_present=");
	serial_print_hex64(reclaim_stats.leaf_present);
	serial_print(" pdpt_freed=");
	serial_print_hex64(reclaim_stats.pdpt_freed);
	serial_print(" pd_freed=");
	serial_print_hex64(reclaim_stats.pd_freed);
	serial_print(" pt_freed=");
	serial_print_hex64(reclaim_stats.pt_freed);
	serial_print(" leaf_freed=");
	serial_print_hex64(reclaim_stats.leaf_freed);
	serial_print("\n");
	serial_print("[FASE41][PMM_AUDIT] orphan_frames=");
	serial_print_hex64(orphan_frames);
	serial_print(" double_free=");
	serial_print_hex64(double_free);
	serial_print(" alive_owner_missing=");
	serial_print_hex64(alive_owner_missing);
	serial_print("\n");
	paging_fase42_checkpoint("destroy-after", (int32_t)p->task.pid);
	p->fase44_audit_state = FASE44_PROC_DESTROYED;
	process_fase43_proc_audit("destroy-after");
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	fase50_trace_proc("process_wait-entry", current_process);
	process_fase43_proc_audit("wait-before");
	process_fase44_list_checkpoint("wait-before");
	/*
	 * waitpid-style: pid > 0 waits for that child; pid == -1 or pid == 0
	 * waits for any child of the caller (process groups not implemented).
	 */
	const int any_child = (pid == (pid_t)-1 || pid == 0);

	if (!current_process) {
		serial_print("[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	for (;;) {
		found_child = 0;
		zombie = NULL;
		irq_flags = process_irq_save();

		for (p = process_list; p; p = p->next) {
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;

			found_child = 1;
			if (p->state == PROCESS_ZOMBIE) {
				zombie = p;
				break;
			}
		}

		if (zombie) {
			int status_val;
			pid_t reaped_pid;
			fase50_trace_proc("process_wait-found-zombie", zombie);

			if (status &&
			    validate_userspace_buffer(status, sizeof(int)) != 0)
			{
				process_irq_restore(irq_flags);
				return -EFAULT;
			}

			status_val = (zombie->exit_code & 0xFF) << 8;
			reaped_pid = zombie->task.pid;
			process_irq_restore(irq_flags);

			fase44_reap_zombie(zombie, current_process->task.pid, "wait");
			fase50_trace_proc("process_wait-after-reap", current_process);
			wait_exit_audit_process_wait_reap(reaped_pid, status_val, status);
			process_fase44_list_checkpoint("wait-after");
			process_fase43_proc_audit("wait-reap");

			if (status)
			{
				if (current_process->mode == KERNEL_MODE)
					*status = status_val;
				else if (copy_to_user(status, &status_val, sizeof(int)) != 0)
					return -EFAULT;
			}
			current_process->wait_status_ptr = NULL;
			current_process->irq_frame_saved = 0;
			serial_print("[FASE41][WAIT] pid=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" child=");
			serial_print_hex32((uint32_t)reaped_pid);
			serial_print(" status=");
			serial_print_hex64((uint64_t)(uint32_t)status_val);
			serial_print("\n");

			return reaped_pid;
		}

		process_irq_restore(irq_flags);

		if (!found_child)
		{
			serial_print("[FASE41][WAIT] pid=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" ret=ECHILD\n");
			return -ECHILD;
		}

		/* Children exist but none are zombies yet */
		if (options & WNOHANG)
		{
			serial_print("[FASE41][WAIT] pid=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" ret=WNOHANG\n");
			return 0;
		}

		if (current_process->mode == USER_MODE)
		{
			wait_exit_audit_process_wait_block(pid, status);
			process_apply_syscall_frame_to_task(&current_process->task,
			                                    &current_process->syscall_frame,
			                                    0);
			current_process->syscall_resume_rax = 0;
			current_process->wait_status_ptr = status;
			current_process->irq_frame_saved = 1;
			wait_exit_audit_classify_user_frame("parent-after-wait-arm", current_process);
		}
		current_process->state = PROCESS_BLOCKED;
		sched_schedule_next();
	}
}



uint64_t create_process_page_directory(void)
{
	uint64_t *pml4;
	uint64_t kernel_cr3;
	uint64_t *kernel_pml4;
	int i;

	/* Allocate page-aligned memory for PML4 */
	pml4 = kmalloc_aligned_try(4096, 4096);
	if (!pml4)
	{
		paging_fase43_note_oom("create_process_page_directory",
				       paging_fase43_classify_current());
		return 0;
	}

	memset(pml4, 0, 4096);
	kernel_cr3 = get_current_page_directory();
	kernel_pml4 = (uint64_t *)kernel_cr3;

	/* Copy ONLY kernel space mappings (not user space)
	 * In x86-64 canonical addressing:
	 * - User space: virtual addresses 0x0000000000000000 - 0x00007FFFFFFFFFFF (PML4 indices 0-255)
	 * - Kernel space: virtual addresses 0xFFFF800000000000 - 0xFFFFFFFFFFFFFFFF (PML4 indices 256-511)
	 * 
	 * We only copy kernel space (indices 256-511) to prevent user processes from
	 * accessing kernel memory. User space entries start empty.
	 */
	for (i = 256; i < 512; i++)
	{
		if (kernel_pml4[i] & PAGE_PRESENT)
			pml4[i] = kernel_pml4[i];
	}

	/*
	 * Map kernel low memory with 4 KiB supervisor pages so timer IRQ (TSS
	 * RSP0), syscall handlers, and kernel text/data are reachable under
	 * process CR3.  Do not cover the user ELF load window (0x400000+): table
	 * entries without PAGE_USER there block user code fetch (Linux requires
	 * PAGE_USER on every level for user mappings).
	 */
	{
		uint64_t id_end = PMM_PHYS_BASE + PMM_PHYS_SIZE;

		if (map_supervisor_identity_low(pml4, 0, 0x00400000UL) != 0)
		{
			kfree_aligned(pml4);
			return 0;
		}
		if (map_supervisor_identity_low(pml4, KEYBOARD_BUFFER_ADDR, id_end) != 0)
		{
			kfree_aligned(pml4);
			return 0;
		}
	}

	/*
	 * Copy canonical kernel-half entries if present (future high-half kernel).
	 */

	/*
	 * Explicitly map framebuffer into process so console output is visible.
	 * Framebuffer is often above 32MB (e.g. 0xFD000000) and may not be
	 * in the copied low-memory mapping.
	 */
#if CONFIG_ENABLE_VBE
	if (video_backend_is_available() && video_backend_get_fb_phys() != 0)
	{
#if !defined(IR0_USERSPACE_INIT_BOOT) || !IR0_USERSPACE_INIT_BOOT
		uint32_t fb_phys = video_backend_get_fb_phys();
		uint32_t fb_size = video_backend_get_fb_size();
		/* Cap: avoid multi-second page-table walks on large framebuffers at spawn. */
		if (fb_size > (4U * 1024U * 1024U))
			fb_size = 4U * 1024U * 1024U;
		for (uint32_t off = 0; off < fb_size; off += 4096)
		{
			uint64_t p = fb_phys + off;
			if (map_page_in_directory(pml4, p, p, PAGE_PRESENT | PAGE_RW) != 0)
				break;
		}
#endif
	}
#endif

	paging_fase42_note_pml4_created((uint64_t)(uintptr_t)pml4);
	process_fase43_note_mm_created();
	return (uint64_t)pml4;
}

void process_init_fd_table(process_t *process)
{
	int i;

	if (!process)
		return;

	/* Initialize all FDs as unused */
	for (i = 0; i < MAX_FDS_PER_PROCESS; i++)
	{
		process->fd_table[i].in_use = false;
		process->fd_table[i].path[0] = '\0';
		process->fd_table[i].flags = 0;
		process->fd_table[i].fd_flags = 0;
		process->fd_table[i].offset = 0;
		process->fd_table[i].vfs_file = NULL;
	}

	/* Setup standard streams */
	process->fd_table[0].in_use = true;
	strncpy(process->fd_table[0].path, "/dev/stdin",
		sizeof(process->fd_table[0].path) - 1);

	process->fd_table[1].in_use = true;
	strncpy(process->fd_table[1].path, "/dev/stdout",
		sizeof(process->fd_table[1].path) - 1);

	process->fd_table[2].in_use = true;
	strncpy(process->fd_table[2].path, "/dev/stderr",
		sizeof(process->fd_table[2].path) - 1);
}
