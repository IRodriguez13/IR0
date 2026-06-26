/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: fase_audit.c
 * Description: FASE43–48 bring-up audit module (IR0_DEBUG_PROC)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "debug/fase_audit.h"
#include "process.h"
#include <config.h>
#include <ir0/arch_port.h>
#include <ir0/debug_runtime.h>
#include <ir0/kmem.h>
#include <ir0/paging.h>
#include <ir0/pipe.h>
#include <ir0/pmm.h>
#include <ir0/serial_io.h>
#include <string.h>

extern void fase48_fd_get_stats(uint64_t *created, uint64_t *destroyed,
				uint64_t *blocked_readers, uint64_t *blocked_writers);

#if IR0_DEBUG_PROC


#define FASE_AUDIT_SLOTS 128

static fase_proc_audit_t fase_audit_slots[FASE_AUDIT_SLOTS];
static int fase_audit_slot_count;

fase_proc_audit_t *fase_audit_get(process_t *p, int create)
{
	int i;

	if (!p)
		return NULL;

	for (i = 0; i < fase_audit_slot_count; i++)
	{
		if (fase_audit_slots[i].proc == p)
			return &fase_audit_slots[i];
	}

	if (!create || fase_audit_slot_count >= FASE_AUDIT_SLOTS)
		return NULL;

	i = fase_audit_slot_count++;
	fase_audit_slots[i].proc = p;
	fase_audit_slots[i].fase44_audit_state = FASE44_PROC_ALIVE;
	fase_audit_slots[i].fase46_fork_generation = 0;
	fase_audit_slots[i].fase46_fork_parent_pid = 0;
	fase_audit_slots[i].fase46_entered_userspace = 0;
	fase_audit_slots[i].fase46_entered_exit = 0;
	fase_audit_slots[i].fase46_entered_wait = 0;
	return &fase_audit_slots[i];
}

void fase_audit_unbind(process_t *p)
{
	int i;

	if (!p)
		return;

	for (i = 0; i < fase_audit_slot_count; i++)
	{
		if (fase_audit_slots[i].proc != p)
			continue;
		fase_audit_slot_count--;
		if (i < fase_audit_slot_count)
			fase_audit_slots[i] = fase_audit_slots[fase_audit_slot_count];
		return;
	}
}

void fase_audit_spawn_init(process_t *p)
{
	fase_proc_audit_t *a = fase_audit_get(p, 1);

	if (a)
		a->fase44_audit_state = FASE44_PROC_ALIVE;
}

void fase_audit_fork_init(process_t *child, process_t *parent)
{
	fase_proc_audit_t *ca = fase_audit_get(child, 1);
	fase_proc_audit_t *pa = parent ? fase_audit_get(parent, 0) : NULL;

	if (!ca)
		return;

	ca->fase44_audit_state = FASE44_PROC_ALIVE;
	ca->fase46_fork_generation = pa ? pa->fase46_fork_generation + 1U : 0U;
	ca->fase46_fork_parent_pid = parent ? parent->task.pid : 0;
	ca->fase46_entered_userspace = 0;
	ca->fase46_entered_exit = 0;
	ca->fase46_entered_wait = 0;
}


static uint64_t fase43_proc_created;
static uint64_t fase43_proc_exited;
static uint64_t fase43_proc_zombie;
static uint64_t fase43_proc_reaped;
static uint64_t fase43_proc_destroyed;
static uint64_t fase43_mm_created;
static uint64_t fase43_mm_destroyed;
static uint64_t fase43_reparent_events;
static uint64_t fase43_reap_events;


const char *fase_audit_state_name(int state)
{
	switch ((process_state_t)state)
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
		serial_print(fase_audit_state_name(p->state));
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
#if !IR0_DEBUG_PROC
	(void)tag;
	return;
#else
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
#endif
}

void process_fase43_live_proc_dump(void)
{
#if !IR0_DEBUG_PROC
	return;
#else
	process_t *p;
	uint64_t live = 0;

	for (p = process_list; p; p = p->next)
	{
		live++;
		serial_print("[FASE43][LIVE_PROC] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" state=");
		serial_print(fase_audit_state_name(p->state));
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
#endif
}
static int fase44_baseline_set;
static uint64_t fase44_baseline_count;
static int fase44_steady_summary_done;
static uint64_t fase45_fork_rollback;
static int fase48_ipc_summary_done;
static uint64_t fase48_fd_baseline;
static int fase47_closure_done;

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

void fase_audit_ref_emit(process_t *p, const char *tag)
{
#if !IR0_DEBUG_PROC
	(void)p;
	(void)tag;
	return;
#else
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
	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		serial_print(fase44_audit_name(fa ? fa->fase44_audit_state : 0));
	}
	serial_print("\n");
#endif
}

void fase_audit_destroy_audit(process_t *p, pid_t parent, uint8_t state_before,
				 uint8_t state_after, int removed_from_list,
				 const char *tag)
{
#if !IR0_DEBUG_PROC
	(void)p;
	(void)parent;
	(void)state_before;
	(void)state_after;
	(void)removed_from_list;
	(void)tag;
	return;
#else
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
#endif
}

void fase_audit_trace_pid(pid_t pid, const char *event)
{
#if !IR0_DEBUG_PROC
	(void)pid;
	(void)event;
	return;
#else
	serial_print("[FASE44][TRACE] pid=");
	serial_print_hex32((uint32_t)pid);
	serial_print(" ");
	serial_print(event);
	serial_print("\n");
#endif
}

void fase_audit_reap_zombie(process_t *child, pid_t parent_pid, const char *tag)
{
	uint8_t before;
	int removed;

	if (!child)
		return;

	{
		fase_proc_audit_t *fa = fase_audit_get(child, 0);

		before = fa ? fa->fase44_audit_state : 0;
	}
	removed = process_remove_from_list(child);
	process_fase50_trace_proc("reap_zombie-removed", child);
	fase_audit_destroy_audit(child, parent_pid, before, FASE44_PROC_REAPED,
			     removed == 0, tag);
	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][REAP] child=");
		serial_print_hex32((uint32_t)child->task.pid);
		serial_print(" parent=");
		serial_print_hex32((uint32_t)parent_pid);
		serial_print(" removed=");
		serial_print_hex64((uint64_t)(int64_t)removed);
		serial_print(" tag=");
		serial_print(tag ? tag : "?");
		serial_print("\n");
	);
	if (removed != 0)
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_DESTROY] child=");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" reason=remove_from_list err=");
			serial_print_hex64((uint64_t)(int64_t)removed);
			serial_print(" tag=");
			serial_print(tag ? tag : "?");
			serial_print("\n");
		);
		return;
	}

	fase_audit_trace_pid(child->task.pid, "REAP");
	{
		fase_proc_audit_t *fa = fase_audit_get(child, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_REAPED;
	}
	fase_audit_ref_emit(child, tag);
	fase43_proc_reaped++;
	fase_audit_trace_pid(child->task.pid, "DESTROY");
	process_fase50_trace_proc("reap_zombie-before-destroy", child);
	process_destroy(child);
	fase_audit_trace_pid(child->task.pid, "FREE");
	kfree(child);
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

void fase_audit_fork_state(pid_t pid, const char *stage)
{
	if (!DEBUG_FORK)
		return;
	serial_print("[FASE45][FORK_STATE] pid=");
	serial_print_hex32((uint32_t)pid);
	serial_print(" stage=");
	serial_print(stage ? stage : "(null)");
	serial_print("\n");
}

void fase_audit_assert_child_not_visible(pid_t pid)
{
	if (!DEBUG_FORK)
		return;
	if (process_find_by_pid(pid) != NULL)
	{
		serial_print("[FASE45][ASSERT] child visible after rollback pid=");
		serial_print_hex32((uint32_t)pid);
		serial_print("\n");
	}
}

void process_fase45_fork_audit(const char *tag)
{
	if (!DEBUG_FORK)
		return;
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

	if (!DEBUG_FORK)
		return;

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
	fase_proc_audit_t *fa;

	if (!p || !phase)
		return;

	if (!IR0_DEBUG_PROC)
		return;

	fa = fase_audit_get(p, 0);
	if (!fa)
		return;

	serial_print("[FASE46][PROC] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" ppid=");
	serial_print_hex32((uint32_t)p->ppid);
	serial_print(" gen=");
	serial_print_hex32(fa->fase46_fork_generation);
	serial_print(" fork_ret=");
	serial_print_hex64((uint64_t)fork_ret);
	serial_print(" phase=");
	serial_print(phase);
	serial_print(" rip=");
	serial_print_hex64(p->task.rip);
	serial_print("\n");

	if (strcmp(phase, "USER_ENTER") == 0)
	{
		if (fa->fase46_entered_userspace)
		{
			serial_print("[FASE46][ASSERT] duplicate USER_ENTER pid=");
			serial_print_hex32((uint32_t)p->task.pid);
			serial_print("\n");
		}
		fa->fase46_entered_userspace = 1;
		fase46_entered_userspace++;
		if (fa->fase46_fork_generation > 0)
			fase46_child_user_enter++;
	}
	else if (strcmp(phase, "EXIT") == 0)
	{
		fa->fase46_entered_exit = 1;
		fase46_exited++;
		if (fa->fase46_fork_generation > 0)
			fase46_child_exited++;
	}
	else if (strcmp(phase, "DESTROY") == 0)
	{
		if (fa->fase46_fork_generation > 0)
			fase46_child_destroyed++;
	}
}

void process_fase46_note_wait(process_t *p)
{
#if !IR0_DEBUG_PROC
	(void)p;
	return;
#else
	if (!p)
		return;

	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (!fa || fa->fase46_entered_wait)
			return;
		fa->fase46_entered_wait = 1;
	}
	fase46_entered_wait++;
	process_fase46_proc_log(p, -1, "WAIT");
#endif
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

	if (!IR0_DEBUG_PROC)
		return;

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
#if !IR0_DEBUG_PROC
	(void)tag;
	return;
#else
	uint64_t cnt = process_list_count();

	if (!fase44_baseline_set && current_process &&
	    current_process->task.pid == 1)
	{
		fase44_baseline_count = cnt;
		fase44_baseline_set = 1;
	}

#if IR0_DEBUG_PROC
	serial_print("[FASE44][LIST] tag=");
	serial_print(tag ? tag : "(null)");
	serial_print(" count=");
	serial_print_hex64(cnt);
	serial_print(" baseline=");
	serial_print_hex64(fase44_baseline_count);
	serial_print(" user_live=");
	serial_print_hex64(process_list_count_user());
	serial_print("\n");
#endif

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
#endif
}

void process_fase44_drain_zombie_children(pid_t ppid)
{
#if !IR0_DEBUG_PROC
	(void)ppid;
	return;
#else
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
		fase_audit_reap_zombie(found, ppid, "drain-zombie");
		process_fase44_list_checkpoint("drain-zombie-after");
	}
#endif
}

void process_fase44_live_summary(const char *tag)
{
	process_t *p;
	uint64_t live = process_list_count();
	uint64_t live_user = process_list_count_user();
	uint64_t zombies = 0;
	int lifecycle_ok;

	if (!IR0_DEBUG_PROC)
		return;

	for (p = process_list; p; p = p->next)
	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (p->state == PROCESS_ZOMBIE)
			zombies++;

		serial_print("[FASE44][LIVE] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" comm=");
		serial_print(p->comm);
		serial_print(" state=");
		serial_print(fase_audit_state_name(p->state));
		serial_print(" audit=");
		serial_print(fase44_audit_name(fa ? fa->fase44_audit_state : 0));
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



void fase_audit_note_proc_created(void) { fase43_proc_created++; }
void fase_audit_note_proc_exited(void) { fase43_proc_exited++; }
void fase_audit_note_proc_zombie(void) { fase43_proc_zombie++; }
void fase_audit_note_proc_destroyed(void) { fase43_proc_destroyed++; }
void fase_audit_note_reparent(void) { fase43_reparent_events++; }
void fase_audit_note_reap_event(void) { fase43_reap_events++; }
void fase_audit_note_fork_rollback(void) { fase45_fork_rollback++; }
void fase_audit_note_scheduled(void) { fase46_scheduled++; }

static uint64_t fase_audit_count_open_fds(process_t *p)
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

uint64_t process_count_open_fds(process_t *p)
{
	return fase_audit_count_open_fds(p);
}

#endif /* IR0_DEBUG_PROC */
