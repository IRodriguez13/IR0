/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: process.c
 * Description: IR0 kernel Process lifecycle management, fork, exit, wait
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <ir0/console.h>
#include <ir0/debug_trap.h>
#include <ir0/debug_runtime.h>
#include <ir0/devfs.h>
#include <ir0/pseudo_fs.h>
#include "process.h"
#include <config.h>
#include <ir0/clock_wait.h>
#include <ir0/scheduler_api.h>
#include <ir0/kmem.h>
#include <ir0/pipe.h>
#include <ir0/paging.h>
#include <ir0/vfs.h>
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
#include <ir0/pmm.h>
#include <ir0/sock_udp.h>
#include "syscalls/process_syscalls.h"
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

#if FASE40_D_AUDIT
static void fase40_d_audit_reap_line(const char *stage, process_t *child,
				     pid_t parent_pid, int removed,
				     const char *tag)
{
	if (!child)
		return;

	serial_print("[FASE40_D_AUDIT][");
	serial_print(stage);
	serial_print("] child=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print(" parent=");
	serial_print_hex32((uint32_t)parent_pid);
	serial_print(" removed=");
	serial_print_hex64((uint64_t)(int64_t)removed);
	serial_print(" tag=");
	serial_print(tag ? tag : "?");
	serial_print(" owns_pml4=");
	serial_print_hex64(child->owns_page_directory);
	serial_print(" pml4=");
	serial_print_hex64((uint64_t)(uintptr_t)child->page_directory);
	serial_print("\n");
}

static void fase40_d_audit_destroy_done(process_t *p,
					const process_reclaim_stats_t *stats,
					uint64_t orphan_frames)
{
	size_t used_frames = 0;

	if (!p || !stats)
		return;

	pmm_stats(NULL, &used_frames, NULL);
	serial_print("[FASE40_D_AUDIT][UNMAP_DONE] pid=");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" mapped=");
	serial_print_hex64(stats->mapped_pages);
	serial_print(" freed=");
	serial_print_hex64(stats->freed_pages);
	serial_print(" missing=");
	serial_print_hex64(stats->missing_pages);
	serial_print(" orphan=");
	serial_print_hex64(orphan_frames);
	serial_print(" pmm_used=");
	serial_print_hex64((uint64_t)used_frames);
	serial_print("\n");
}
#endif

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

static bool process_va_ranges_overlap(uintptr_t a_start, size_t a_len,
				      uintptr_t b_start, size_t b_len)
{
	uintptr_t a_end;
	uintptr_t b_end;

	if (a_len == 0 || b_len == 0)
		return false;

	a_end = a_start + a_len;
	b_end = b_start + b_len;
	return a_start < b_end && b_start < a_end;
}

bool process_user_va_range_overlaps(process_t *proc, uintptr_t addr, size_t length)
{
	struct mmap_region *r;

	if (!proc || length == 0)
		return false;

	if (proc->heap_end > proc->heap_start &&
	    process_va_ranges_overlap(addr, length, proc->heap_start,
				      (size_t)(proc->heap_end - proc->heap_start)))
		return true;

	if (proc->stack_size > 0 &&
	    process_va_ranges_overlap(addr, length, proc->stack_start,
				      (size_t)proc->stack_size))
		return true;

	for (r = proc->mmap_list; r; r = r->next)
	{
		if (process_va_ranges_overlap(addr, length, (uintptr_t)r->addr,
					      r->length))
			return true;
	}

	return false;
}

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

void process_fase50_trace_proc(const char *stage, process_t *p)
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
	serial_print(p ? fase_audit_state_name((int)p->state) : "NULL");
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
#if !IR0_DEBUG_WAIT
	(void)tag;
	(void)p;
	return;
#else
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
#endif
}

static void wait_exit_audit_process_exit(process_t *dying, process_t *parent,
                                         int parent_state_before)
{
#if !IR0_DEBUG_WAIT
	(void)dying;
	(void)parent;
	(void)parent_state_before;
	return;
#else
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
#endif
}

static void wait_exit_audit_process_wait_block(pid_t wait_pid, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)wait_pid;
	(void)status;
	return;
#else
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
#endif
}

static void wait_exit_audit_process_wait_reap(pid_t reaped_pid, int status_val, int *status)
{
#if !IR0_DEBUG_WAIT
	(void)reaped_pid;
	(void)status_val;
	(void)status;
	return;
#else
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
#endif
}

static void process_release_fds(process_t *p, const char *pipe_trace_op)
{
	int i;

	if (!p)
		return;
	process_fase50_trace_proc("process_release_fds-begin", p);

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
			if (DEBUG_FASE50)
			{
				serial_print("[FASE50][FDREL] stage=close_pipe pid=");
				serial_print_hex32((uint32_t)p->task.pid);
				serial_print(" fd=");
				serial_print_hex64((uint64_t)i);
				serial_print(" refs_before=");
				serial_print_hex64((uint64_t)refs_before);
				serial_print(" end=");
				serial_print_hex64((uint64_t)e->pipe_end);
				serial_print("\n");
			}
			pipe_close_end(pip, e->pipe_end);
			pipe_wake_all(pip);
			e->vfs_file = NULL;
		}
		else if (i <= 2)
			goto clear_fd;
		else if (e->is_socket && e->vfs_file)
		{
			sock_udp_release((struct sock_udp *)e->vfs_file);
			e->vfs_file = NULL;
		}
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (node)
				devfs_close_node(node);
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			if (bind->refs > 0)
				bind->refs--;
			if (bind->refs == 0)
			{
				(void)pseudo_fs_release_ops(
					(const pseudo_fs_ops_t *)bind->ops,
					bind->ctx, bind->dynamic);
				kfree(bind);
			}
			e->vfs_file = NULL;
		}
		else if (e->vfs_file)
		{
			vfs_close((struct vfs_file *)e->vfs_file);
			e->vfs_file = NULL;
		}

clear_fd:
		e->in_use = false;
		e->is_pipe = false;
		e->is_socket = false;
		e->is_devfs = false;
		e->is_pseudo = false;
		e->dev_device_id = 0;
		e->pipe_end = -1;
		e->path[0] = '\0';
		e->flags = 0;
		e->fd_flags = 0;
		e->offset = 0;
	}
	process_fase50_trace_proc("process_release_fds-end", p);
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
uint64_t *process_pt_child(uint64_t *table, size_t index)
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
	pmm_owner_audit(&orphan_frames, &double_free, &alive_owner_missing);
	if (IR0_DEBUG_PROC)
	{
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
		serial_print("[FASE41][PMM_AUDIT] orphan_frames=");
		serial_print_hex64(orphan_frames);
		serial_print(" double_free=");
		serial_print_hex64(double_free);
		serial_print(" alive_owner_missing=");
		serial_print_hex64(alive_owner_missing);
		serial_print("\n");
	}
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
 * irq_save_user_frame - Copy the full user context from the IRQ stub stack into
 * the current task.
 *
 * @frame: pointer to the iretq frame on the ISR stub stack
 *         (frame[0..6] = int_no, err, RIP, CS, RFLAGS, RSP, SS). The 15 saved
 *         GPRs sit immediately BELOW it (isr_common_stub_64 push order), so they
 *         are reachable at frame[-1..-15]:
 *           [-1]=rax [-2]=rcx [-3]=rdx [-4]=rbx [-5]=rbp [-6]=rsi [-7]=rdi
 *           [-8]=r8  [-9]=r9  [-10]=r10 [-11]=r11 [-12]=r12 [-13]=r13
 *           [-14]=r14 [-15]=r15
 *
 * Saving the GPRs (not just RIP/RSP/RFLAGS) keeps task_t coherent if a user task
 * is ever resumed via switch_context_x64 .user_iretq_resume from this snapshot
 * (e.g. when the IRQ preempt path is wired): a partial save would resume the
 * task with stale GPRs and corrupt user computation.
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

	/* Full user GPR set from the stub stack (below the iretq frame). */
	p->task.rax = frame[-1];
	p->task.rcx = frame[-2];
	p->task.rdx = frame[-3];
	p->task.rbx = frame[-4];
	p->task.rbp = frame[-5];
	p->task.rsi = frame[-6];
	p->task.rdi = frame[-7];
	p->task.r8 = frame[-8];
	p->task.r9 = frame[-9];
	p->task.r10 = frame[-10];
	p->task.r11 = frame[-11];
	p->task.r12 = frame[-12];
	p->task.r13 = frame[-13];
	p->task.r14 = frame[-14];
	p->task.r15 = frame[-15];

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
int process_kernel_stack_alloc(process_t *p)
{
	void *base;

	if (!p)
		return -EINVAL;
	if (p->kstack_base)
		return 0;

	base = kmalloc_aligned_try(IR0_PROC_KSTACK_SIZE, 16);
	if (!base)
		return -ENOMEM;

	memset(base, 0, IR0_PROC_KSTACK_SIZE);
	p->kstack_base = base;
	p->kstack_top = (uint64_t)(uintptr_t)base + IR0_PROC_KSTACK_SIZE;
	p->saved_user_rsp = 0;
	return 0;
}

void process_kernel_stack_free(process_t *p)
{
	if (!p || !p->kstack_base)
		return;

	kfree_aligned(p->kstack_base);
	p->kstack_base = NULL;
	p->kstack_top = 0;
	p->saved_user_rsp = 0;
}

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
	proc->tgid = proc->task.pid;
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
	process_cred_init_groups(proc);
	if (proc->cwd[0] != '/')
	{
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
		proc->signal_sa_flags[i] = 0;
		proc->signal_sa_mask[i] = 0;
	}
	proc->robust_list = NULL;

	/* Private kernel stack for syscall/IRQ entry (see IR0_PROC_KSTACK_SIZE). */
	if (process_kernel_stack_alloc(proc) != 0)
	{
		serial_print("SERIAL: spawn: kernel stack alloc failed\n");
		if (proc->mode != USER_MODE && proc->stack_start)
		{
			kfree((void *)proc->stack_start);
			proc->stack_start = 0;
		}
		else if (proc->owns_page_directory)
		{
			process_unmap_user_pages_all(proc->page_directory, NULL);
		}
		goto fail_proc;
	}

	/* Add to process list */
	{
		uint64_t irq_flags = process_irq_save();
		proc->next = process_list;
		process_list = proc;
		process_irq_restore(irq_flags);
	}

	fase_audit_note_proc_created();
#if IR0_DEBUG_PROC
	process_fase43_proc_audit("spawn-after");
	fase_audit_spawn_init(proc);
	fase_audit_trace_pid(proc->task.pid, "CREATED");
	fase_audit_ref_emit(proc, "spawn");
	process_fase44_list_checkpoint("spawn-after");
#endif

	/* Add to scheduler */
	sched_add_process(proc);

	return proc->task.pid;

fail_proc:
	process_kernel_stack_free(proc);
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
extern uint64_t fase29_entry_rip;

/*
 * process_capture_syscall_frame - Snapshot user GPRs at syscall dispatch entry.
 *
 * Must run before nested C calls (fork, wait4, execve path) clobber the
 * syscall kernel stack layout (Linux pt_regs at entry).
 */

/*
 * process_capture_syscall_frame_at_entry - Linux SAVE_ALL at syscall entry.
 *
 * @frame_base: RSP at the rbx slot (see syscall_insn_entry_64.asm).
 * @rip_hw: optional hardware user RIP; asm does not pass this today.
 */
void process_capture_syscall_frame_at_entry(uint64_t *frame_base, uint64_t rip_hw)
{
	process_t *p = current_process;
	syscall_user_frame_t *sf;

	if (!frame_base || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	/*
	 * frame_base[7] is the user RIP (rcx) pushed at this syscall entry.
	 * fase29_entry_rip is written at the *previous* syscall's sysret and must
	 * not override the current entry snapshot (breaks fork child iretq).
	 * rip_hw is not passed from asm today; only use as last resort.
	 */
	sf->rip = frame_base[7];
	if (!sf->rip && rip_hw)
		sf->rip = rip_hw;
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
	/*
	 * Mark this task as having a fresh Linux pt_regs snapshot for this entry.
	 * Only the `syscall` insn path reaches here; int 0x80 tasks never set it,
	 * which keeps the cooperative syscall_frame resume restricted to musl.
	 */
	p->syscall_frame_fresh = 1;
	process_sync_task_user_ip_from_syscall_frame(p);
}

void process_sync_task_user_ip_from_syscall_frame(process_t *p)
{
	syscall_user_frame_t *sf;

	if (!p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	p->task.rip = sf->rip;
	p->task.rsp = sf->rsp;
	p->task.rflags = ir0_rflags_sanitize_user(sf->rflags | 2ULL);
	p->task.rcx = sf->rip;
	p->task.r11 = p->task.rflags;
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

/*
 * process_syscall_restore_exit_regs - Linux RESTORE_ALL before sysret.
 *
 * Repopulate the syscall stack pt_regs mirror from current->syscall_frame so
 * nested C in fork/wait4 cannot clobber user GPRs (musl TLS in %rdx, etc.).
 *
 * @stack_r9_slot: RSP at the saved-r9 word (see syscall_insn_entry_64.asm).
 */
void process_syscall_restore_exit_regs(uint64_t *stack_r9_slot)
{
	process_t *p = current_process;
	const syscall_user_frame_t *sf;

	if (!stack_r9_slot || !p || p->mode != USER_MODE)
		return;

	sf = &p->syscall_frame;
	stack_r9_slot[0] = sf->r9;
	stack_r9_slot[1] = sf->r8;
	stack_r9_slot[2] = sf->r10;
	stack_r9_slot[3] = sf->rdx;
	stack_r9_slot[4] = sf->rsi;
	stack_r9_slot[5] = sf->rdi;
	stack_r9_slot[6] = sf->rbx;
	stack_r9_slot[7] = sf->rbp;
	stack_r9_slot[8] = sf->r12;
	stack_r9_slot[9] = sf->r13;
	stack_r9_slot[10] = sf->r14;
	stack_r9_slot[11] = sf->r15;
	stack_r9_slot[12] = sf->rflags;
	stack_r9_slot[13] = sf->rip;
	p->fork_resync_syscall_stack = 0;
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
 * process_arm_coop_resched_resume - Arm a cooperative in-syscall reschedule to
 * resume via the saved syscall_frame (fresh iretq) instead of kernel_ret on the
 * shared global syscall stack. Unlike wait4, there is no zombie child to reap,
 * so coop_resched_resume tells arch_context_switch to skip the reap step.
 * Only valid for syscall-insn tasks (syscall_frame_fresh).
 */
void process_arm_coop_resched_resume(process_t *p, uint64_t rax)
{
	if (!p || p->mode != USER_MODE)
		return;

	process_apply_syscall_frame_to_task(&p->task, &p->syscall_frame, rax);
	p->syscall_resume_rax = rax;
	p->coop_resched_resume = 1;
	p->irq_frame_saved = 1;
}

/*
 * process_clear_in_thread_syscall_block - Drop irq_frame_saved after blocking
 * syscalls that resume inside the syscall handler (poll/pipe read loops), not
 * via arch_switch_to_user_task.
 */
void process_clear_in_thread_syscall_block(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
}

void process_reset_blocked_syscall_state(process_t *p)
{
	if (!p)
		return;

	p->irq_frame_saved = 0;
	p->poll_resume_via_arch = 0;
	p->coop_resched_resume = 0;
	p->syscall_resume_rax = 0;
	p->syscall_interrupted = 0;
	p->wait_status_ptr = NULL;
	p->wait_blocked = 0;
	p->wait_blocked = 0;
	p->wait_target_pid = 0;
	p->wait_options = 0;
	p->wait_resume_child_pid = 0;
	p->poll_waiter = NULL;
	p->clock_wait_armed = 0;
	p->clock_wait_deadline_ms = IR0_CLOCK_WAIT_DISARMED;
}

/*
 * process_arm_kernel_syscall_sleep - Mark task as ring-0 for switch_context resume.
 * Used when blocking inside a syscall without irq_frame_saved user return.
 */
void process_arm_kernel_syscall_sleep(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	p->task.cs = KERNEL_CODE_SEL;
	p->task.ss = KERNEL_DATA_SEL;
	p->task.ds = KERNEL_DATA_SEL;
	p->task.es = KERNEL_DATA_SEL;
	p->task.fs = KERNEL_DATA_SEL;
	p->task.gs = KERNEL_DATA_SEL;
}

void process_restore_user_task_segments(process_t *p)
{
	if (!p || p->mode != USER_MODE)
		return;

	p->task.cs = USER_CODE_SEL;
	p->task.ss = USER_DATA_SEL;
	p->task.ds = USER_DATA_SEL;
	p->task.es = USER_DATA_SEL;
	p->task.fs = USER_DATA_SEL;
	p->task.gs = USER_DATA_SEL;
}

#if defined(__x86_64__) || defined(__amd64__)
void process_save_user_context_from_irq_frame(uint64_t *gpr_stack)
{
	/*
	 * gpr_stack points at the saved-RAX slot on the ISR stub stack; the
	 * iretq frame begins 15 qwords above (see isr_common_stub_64 / sched_resched.c).
	 */
	if (!gpr_stack)
		return;

	irq_save_user_frame(gpr_stack + 15);
}
#endif

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

	if (!DEBUG_FORK)
		return;
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

	if (!DEBUG_FORK)
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

	if (DEBUG_FORK)
	{
		serial_print("[FORK_RESTORE][FIXUP] child_task=");
		serial_print_hex64(fork_restore_audit.task_ptr);
		serial_print(" syscall_frame=");
		serial_print_hex64((uint64_t)(uintptr_t)&parent->syscall_frame);
		serial_print(" rax_slot_addr=");
		serial_print_hex64(fork_restore_audit.rax_slot_addr);
		serial_print(" task_rax_before=");
		serial_print_hex64(rax_before);
	}

	process_apply_syscall_frame_to_task(&child->task, &parent->syscall_frame, 0);
	process_apply_syscall_frame_to_task(&parent->task, &parent->syscall_frame,
	                                    (uint64_t)child->task.pid);

	fork_restore_audit.task_rax_at_fixup = child->task.rax;
	fork_restore_audit.rax_slot_mem = child->task.rax;

	if (DEBUG_FORK)
	{
		serial_print(" task_rax_after=");
		serial_print_hex64(child->task.rax);
		serial_print(" slot_readback=");
		serial_print_hex64(*(uint64_t *)(uintptr_t)fork_restore_audit.rax_slot_addr);
		serial_print("\n");
	}
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

	if (!DEBUG_FORK)
		return;

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
	if (!DEBUG_FORK)
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

	if (DEBUG_FORK && !fork_branch.classified)
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
	/* Parent RESTORE_ALL uses syscall_frame captured at fork syscall entry. */
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
	child->tgid = child_pid;
	child->ppid = parent->task.pid;
	child->state = PROCESS_READY;
	child->next = NULL;
	child->saved_context = NULL;
	child->poll_waiter = NULL;
	child->poll_resume_via_arch = 0;
	child->fork_pending_child = NULL;
	child->fork_resync_syscall_stack = 0;
	child->irq_frame_saved = 0;
	child->coop_resched_resume = 0;
	child->syscall_frame_fresh = 0;
	child->wait_blocked = 0;
	child->wait_target_pid = 0;
	child->wait_options = 0;
	child->wait_resume_child_pid = 0;
	child->wait_status_ptr = NULL;
	child->syscall_resume_rax = 0;
#if IR0_DEBUG_PROC
	fase_audit_fork_init(child, parent);
#endif
	child->page_directory = NULL;
	child->owns_page_directory = 0;
	child->mmap_list = NULL;
	memset(child->fd_table, 0, sizeof(child->fd_table));

	/*
	 * memcpy copied the parent's kernel-stack pointer; the child needs its own
	 * private kernel stack (a shared one would corrupt both on concurrent
	 * syscalls). Reset before allocating so a failure cannot free parent's.
	 */
	child->kstack_base = NULL;
	child->kstack_top = 0;
	child->saved_user_rsp = 0;
	if (process_kernel_stack_alloc(child) != 0)
	{
		fase_audit_fork_state(child_pid, "FAILED");
		kfree(child);
		return NULL;
	}

	*child_pid_out = child_pid;
	fase_audit_fork_state(child_pid, "CREATED");
	return child;
}

static int fork_child_mm_create(process_t *child)
{
	if (!child)
		return -ENOMEM;

	child->page_directory = (uint64_t *)create_process_page_directory();
	if (!child->page_directory)
	{
		fase_audit_fork_state(child->task.pid, "FAILED");
		return -ENOMEM;
	}

	child->owns_page_directory = 1;
	child->task.cr3 = (uint64_t)(uintptr_t)child->page_directory;
	fase_audit_fork_state(child->task.pid, "MM_CREATED");
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
		else if (e->is_socket && e->vfs_file)
			sock_udp_acquire((struct sock_udp *)e->vfs_file);
		else if (e->is_devfs)
		{
			devfs_node_t *node = devfs_find_node_by_id(e->dev_device_id);

			if (node)
				node->ref_count++;
		}
		else if (e->is_pseudo && e->vfs_file)
		{
			pseudo_fd_bind_t *bind = (pseudo_fd_bind_t *)e->vfs_file;

			bind->refs++;
		}
		else if (e->vfs_file)
			vfs_file_acquire((struct vfs_file *)e->vfs_file);
	}
	fase_audit_fork_state(child->task.pid, "FILES_CLONED");
	return 0;
}

static int fork_attach_pending_child(process_t *child, process_t *parent)
{
	uint64_t irq_flags;

	if (!child || !parent)
		return -EINVAL;

	irq_flags = process_irq_save();
	child->next = process_list;
	process_list = child;
	process_irq_restore(irq_flags);

	child->state = PROCESS_BLOCKED;
	parent->fork_pending_child = child;
	fase_audit_fork_state(child->task.pid, "DEFERRED");
	return 0;
}

void process_fork_wake_pending(process_t *parent)
{
	process_t *child;

	if (!parent)
		return;

	child = parent->fork_pending_child;
	if (!child)
		return;

	parent->fork_pending_child = NULL;
	child->state = PROCESS_READY;
	sched_add_process(child);
	fase_audit_fork_state(child->task.pid, "SCHEDULED");
	fase_audit_note_scheduled();
}

static void fork_rollback(process_t *child, pid_t child_pid, int enqueued)
{
	if (!child)
		return;

	fase_audit_fork_state(child_pid, "FAILED");
	fase_audit_fork_state(child_pid, "ROLLBACK");
	fase_audit_note_fork_rollback();

	if (enqueued)
	{
		process_t *parent = current_process;

		if (parent && parent->fork_pending_child == child)
			parent->fork_pending_child = NULL;

		if (child->state == PROCESS_READY || child->state == PROCESS_RUNNING)
			sched_remove_process(child);
		(void)process_remove_from_list(child);
	}

	fase_audit_assert_child_not_visible(child_pid);

	process_release_fds(child, "FORK_ROLLBACK");
	fork_destroy_child_mm(child);
	fork_free_mmap_list(child);

	fase_audit_fork_state(child_pid, "DESTROYED");
	process_kernel_stack_free(child);
	kfree(child);

	fase_audit_assert_child_not_visible(child_pid);
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
#if IR0_DEBUG_PROC
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
#endif

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
	fase_audit_fork_state(child_pid, "MEMORY_CLONED");

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
		/* Parent must not resume userspace with rax=0 after child ran first. */
		parent->task.rax = (uint64_t)child_pid;
		fork_fixup_user_syscall_return(parent, child);
		arch_set_fs_base(parent->fs_base);
#if IR0_DEBUG_PROC
		process_fase46_proc_log(parent, (int64_t)child_pid, "AFTER_FORK");
		process_fase46_proc_log(child, 0, "USER_ENTER");
#endif
	}
#endif

	if (fork_attach_pending_child(child, parent) != 0)
	{
		fork_rollback(child, child_pid, 0);
		return -ENOMEM;
	}

	fase_audit_note_proc_created();
#if IR0_DEBUG_PROC
	process_fase43_proc_audit("fork-after");
	fase_audit_trace_pid(child_pid, "CREATED");
	fase_audit_ref_emit(child, "fork");
	process_fase44_list_checkpoint("fork-after");
	process_fase45_fork_audit("fork-after");
	paging_fase42_checkpoint("fork-after", (int32_t)child_pid);
#endif

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
			fase_proc_audit_t *fa = fase_audit_get(child, 0);
			uint8_t audit_st = fa ? fa->fase44_audit_state : 0;

			child->ppid = 1;
			fase_audit_note_reparent();
			fase_audit_destroy_audit(child, dying_parent->task.pid,
					     audit_st, audit_st, 0, "reparent");
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

void process_reap_zombie_child(process_t *child)
{
	int removed;

	if (!child)
		return;

	removed = process_remove_from_list(child);
	FASE40_D_AUDIT_LOG(fase40_d_audit_reap_line("REAP_CHILD", child, 0, removed,
						    "reap_zombie_child"));
	if (removed != 0)
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_DESTROY] child=");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print(" reason=remove_from_list err=");
			serial_print_hex64((uint64_t)(int64_t)removed);
			serial_print("\n");
		);
		return;
	}
	process_destroy(child);
	kfree(child);
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
			fase_audit_note_reap_event();
			process_fase43_proc_audit("reap-zombie");
			fase_audit_reap_zombie(child, parent->task.pid, "reap-zombie");
		}
		
		child = next;
	}
	process_fase44_list_checkpoint("reap-zombie-after");
}

int process_wait_child_matches_blocked_target(const process_t *parent,
					    pid_t child_pid)
{
	pid_t target;
	int any_child;

	if (!parent || child_pid <= 0 || !parent->wait_blocked)
		return 0;

	target = parent->wait_target_pid;
	any_child = (target == (pid_t)-1 || target == 0);
	if (!any_child && child_pid != target)
		return 0;

	return 1;
}

int process_child_wait_status_word(const process_t *child)
{
	if (!child)
		return 0;
	if (child->exit_signal > 0)
		return child->exit_signal & 0x7f;
	return (child->exit_code & 0xff) << 8;
}

static void process_wait_wake_blocked_parent(process_t *parent, process_t *child);

static int process_signal_is_default_fatal(process_t *p, int sig)
{
	void (*handler)(int);

	if (!p)
		return 0;
	if (sig == SIGKILL)
		return 1;
	if (sig != SIGTERM)
		return 0;
	if (p->signal_ignored & SIGNAL_MASK(SIGTERM))
		return 0;
	handler = p->signal_handlers[SIGTERM];
	if (handler && handler != SIG_DFL && handler != SIG_IGN)
		return 0;
	return 1;
}

int process_signal_default_kill(process_t *dying, int sig)
{
	process_t *parent;
	int parent_state_before = -1;

	if (!dying || !process_signal_is_default_fatal(dying, sig))
		return 0;

	dying->irq_frame_saved = 0;
	process_reap_zombies(dying);
	process_reparent_children(dying);
	process_release_fds(dying, "EXIT_CLOSE");

	dying->signal_pending &= ~SIGNAL_MASK(sig);
	dying->exit_signal = sig;
	dying->exit_code = 0;
	dying->state = PROCESS_ZOMBIE;
	sched_remove_process(dying);

#if IR0_DEBUG_PROC
	serial_print("[SIGTERM_AUDIT] process_signal_default_kill pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" sig=");
	serial_print_hex32((uint32_t)sig);
	serial_print(" wait_status=");
	serial_print_hex32((uint32_t)process_child_wait_status_word(dying));
	serial_print("\n");
#endif

	if (dying->ppid > 0)
	{
		parent = process_find_by_pid(dying->ppid);
		if (parent)
			parent_state_before = parent->state;
		if (parent && parent->state != PROCESS_ZOMBIE)
		{
			send_signal(parent->task.pid, SIGCHLD);
			if (parent->state == PROCESS_BLOCKED ||
			    parent->wait_blocked)
				process_wait_wake_blocked_parent(parent, dying);
		}
		wait_exit_audit_process_exit(dying, parent, parent_state_before);
	}
	else
	{
		wait_exit_audit_process_exit(dying, NULL, -1);
	}

	return 1;
}

static void process_wait_wake_blocked_parent(process_t *parent, process_t *child)
{
	int status_val;
	int *status_ptr;
	int copy_ret;

	if (!parent || !child || !parent->wait_blocked)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child->task.pid))
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_NOT_TARGET] parent=");
			serial_print_hex32((uint32_t)parent->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)parent->wait_target_pid);
			serial_print(" candidate=");
			serial_print_hex32((uint32_t)child->task.pid);
			serial_print("\n");
		);
		return;
	}

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][CHILD_EXIT] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" target=");
		serial_print_hex32((uint32_t)parent->wait_target_pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child->task.pid);
		serial_print("\n");
	);

	if (!parent->irq_frame_saved)
	{
		/*
		 * Blocked via process_arm_kernel_syscall_sleep: keep kernel CS/SS so
		 * switch_context_x64 resumes with kernel_ret into process_wait, not
		 * user iretq with stale task.rax (placeholder 0 at block time).
		 */
		parent->state = PROCESS_READY;
		sched_add_process(parent);
		sched_promote_process(parent);
		return;
	}

	status_val = process_child_wait_status_word(child);
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
#if IR0_DEBUG_PROC
	serial_print("[SIGTERM_AUDIT] wait_wake parent=");
	serial_print_hex32((uint32_t)parent->task.pid);
	serial_print(" child=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print(" status=");
	serial_print_hex32((uint32_t)status_val);
	serial_print(" rax=");
	serial_print_hex32((uint32_t)child->task.pid);
	serial_print("\n");
#endif
	parent->wait_resume_child_pid = child->task.pid;
	parent->syscall_resume_rax = (uint64_t)child->task.pid;
	parent->state = PROCESS_READY;
	sched_add_process(parent);
	sched_promote_process(parent);
}

void process_reap_zombie_on_wait_resume(process_t *parent, pid_t child_pid)
{
	process_t *child;
	size_t used_frames_before = 0;
	size_t used_frames_after = 0;

	if (!parent || child_pid <= 0)
		return;

	if (!process_wait_child_matches_blocked_target(parent, child_pid))
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][REAP_SKIP_NOT_TARGET] parent=");
			serial_print_hex32((uint32_t)parent->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)parent->wait_target_pid);
			serial_print(" resume_child=");
			serial_print_hex32((uint32_t)child_pid);
			serial_print("\n");
		);
		return;
	}

	child = process_find_by_pid(child_pid);
	if (!child || child->state != PROCESS_ZOMBIE ||
	    child->ppid != parent->task.pid)
		return;

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][REAP_MATCH] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child_pid);
		serial_print("\n");
	);

	pmm_stats(NULL, &used_frames_before, NULL);
	process_fase43_proc_audit("wait-resume-before-reap");
	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][WAIT_RESUME_REAP] parent=");
		serial_print_hex32((uint32_t)parent->task.pid);
		serial_print(" child=");
		serial_print_hex32((uint32_t)child_pid);
		serial_print(" resume_rax=");
		serial_print_hex64(parent->syscall_resume_rax);
		serial_print("\n");
	);
	fase_audit_reap_zombie(child, parent->task.pid, "wait-resume");
	pmm_stats(NULL, &used_frames_after, NULL);
	process_fase44_list_checkpoint("wait-resume-after");
	process_fase43_proc_audit("wait-resume-after-reap");
	if (IR0_DEBUG_PROC)
	{
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
	}
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
	process_fase50_trace_proc("process_exit-entry", dying);
	dying->irq_frame_saved = 0;
	if (IR0_DEBUG_WAIT)
		serial_print("[WAIT_EXIT_AUDIT][CLASSIFY] ZOMBIE_IRQ_SAVED_CLEARED\n");

	/* Before becoming a zombie:
	 * 1. Reparent all children to init (PID 1) to avoid orphaned processes
	 * 2. Clean up any zombie children we were waiting for
	 */
	process_reap_zombies(dying);
	process_reparent_children(dying);

	process_release_fds(dying, "EXIT_CLOSE");

#if IR0_DEBUG_PROC
	process_fase46_proc_log(dying, (int64_t)(uint32_t)code, "EXIT");
	process_fase44_list_checkpoint("exit-before");
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_EXITING;
	}
	fase_audit_trace_pid(dying->task.pid, "EXIT");
	fase_audit_ref_emit(dying, "exit");
	process_fase43_proc_audit("exit-before");
#endif

	/* Mark as zombie */
	dying->state = PROCESS_ZOMBIE;
	if (dying->exit_signal == 0)
		dying->exit_code = code;
	else
		dying->exit_code = 0;
#if IR0_DEBUG_PROC
	if (dying->exit_signal > 0)
	{
		serial_print("[SIGTERM_AUDIT] process_exit pid=");
		serial_print_hex32((uint32_t)dying->task.pid);
		serial_print(" exit_signal=");
		serial_print_hex32((uint32_t)dying->exit_signal);
		serial_print(" wait_status=");
		serial_print_hex32((uint32_t)process_child_wait_status_word(dying));
		serial_print("\n");
	}
#endif
#if IR0_DEBUG_PROC
	{
		fase_proc_audit_t *fa = fase_audit_get(dying, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_ZOMBIE;
	}
	fase_audit_trace_pid(dying->task.pid, "ZOMBIE");
#endif
	fase_audit_note_proc_exited();
	fase_audit_note_proc_zombie();
#if IR0_DEBUG_PROC
	paging_fase42_checkpoint("exit-before", (int32_t)dying->task.pid);
#endif
	for (struct mmap_region *r = dying->mmap_list; r; r = r->next)
		vmas++;
	pmm_stats(&total_frames, &used_frames, NULL);
	if (IR0_DEBUG_PROC)
	{
		serial_print("[FASE41][EXIT] pid=");
		serial_print_hex32((uint32_t)dying->task.pid);
		serial_print(" vmas=");
		serial_print_hex64(vmas);
		serial_print(" used_frames=");
		serial_print_hex64((uint64_t)used_frames);
		serial_print(" total_frames=");
		serial_print_hex64((uint64_t)total_frames);
		serial_print("\n");
	}
#if CONFIG_DEBUG_FASE50
	serial_print("[PROCESS] exit pid=");
	serial_print_hex32((uint32_t)dying->task.pid);
	serial_print(" code=");
	serial_print_hex64((uint64_t)(uint32_t)code);
	serial_print("\n");
#endif

	process_fase43_proc_audit("exit-after");
#if IR0_DEBUG_PROC
	if (dying->task.pid == 1)
	{
		process_fase44_drain_zombie_children(1);
		process_fase43_live_proc_dump();
		process_fase44_live_summary("init-exit");
	}
	process_fase44_list_checkpoint("exit-after");
#endif

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
			if (parent->state == PROCESS_BLOCKED ||
			    parent->wait_blocked)
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
	process_fase50_trace_proc("process_exit-before-schedule", dying);

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
	ir0_clock_wait_disarm(p);

	process_fase46_proc_log(p, -1, "DESTROY");
	fase_audit_ref_emit(p, "destroy");
	fase_audit_note_proc_destroyed();
	process_fase43_proc_audit("destroy-before");

#if CONFIG_DEBUG_FASE50
	serial_print("[PROCESS] destroy PID ");
	serial_print_hex32((uint32_t)p->task.pid);
	serial_print(" (fd cleanup)\n");
#endif

	process_release_fds(p, "DESTROY");

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][DESTROY] pid=");
		serial_print_hex32((uint32_t)p->task.pid);
		serial_print(" owns_pml4=");
		serial_print_hex64(p->owns_page_directory);
		serial_print(" pml4=");
		serial_print_hex64((uint64_t)(uintptr_t)p->page_directory);
		serial_print("\n");
	);

	/* Unmap all user pages in this process's PML4 (reaper may run under another CR3) */
	if (p->page_directory && p->owns_page_directory)
		process_unmap_user_pages_all(p->page_directory, &reclaim_stats);
	else
	{
		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][UNMAP_SKIP] pid=");
			serial_print_hex32((uint32_t)p->task.pid);
			serial_print(" owns_pml4=");
			serial_print_hex64(p->owns_page_directory);
			serial_print(" pml4=");
			serial_print_hex64((uint64_t)(uintptr_t)p->page_directory);
			serial_print("\n");
		);
	}
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

	/* Release the private kernel stack (zombie is off-CPU; not in use). */
	process_kernel_stack_free(p);

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
	FASE40_D_AUDIT_LOG(fase40_d_audit_destroy_done(p, &reclaim_stats, orphan_frames));
#if IR0_DEBUG_PMM
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
#endif
	paging_fase42_checkpoint("destroy-after", (int32_t)p->task.pid);
	{
		fase_proc_audit_t *fa = fase_audit_get(p, 0);

		if (fa)
			fa->fase44_audit_state = FASE44_PROC_DESTROYED;
	}
	fase_audit_unbind(p);
	process_fase43_proc_audit("destroy-after");
}

int process_wait(pid_t pid, int *status, int options)
{
	process_t *p;
	int found_child;
	process_t *zombie;
	uint64_t irq_flags;
	process_fase50_trace_proc("process_wait-entry", current_process);
	process_fase43_proc_audit("wait-before");
	process_fase44_list_checkpoint("wait-before");
	/*
	 * wait4 contract (D1.17):
	 *   pid > 0  — block until that child is ZOMBIE, then reap only that pid.
	 *   pid -1/0 — any child of this process (groups not implemented).
	 *   WNOHANG  — 0 if no matching zombie yet (never ECHILD when children exist).
	 *   ECHILD   — no matching child relationship at all.
	 * User-mode block stores wait_target_pid; wake/resume must not complete or
	 * reap a different child (see process_wait_wake_blocked_parent / wait-resume).
	 */
	const int any_child = (pid == (pid_t)-1 || pid == 0);

	if (!current_process) {
		serial_print("[ERROR] process_wait called without current process context\n");
		return -ESRCH;
	}

	/*
	 * wait_options for USER_MODE is seeded in sys_wait4 from pt_regs (rdx).
	 * Do not copy from the stack parameter here — it may already be clobbered.
	 */

	FASE40_D_AUDIT_LOG(
		serial_print("[FASE40_D_AUDIT][WAIT_BEGIN] parent=");
		serial_print_hex32((uint32_t)current_process->task.pid);
		serial_print(" target=");
		serial_print_hex32((uint32_t)pid);
		serial_print(" options=");
		serial_print_hex32((uint32_t)(current_process->mode == USER_MODE
					      ? current_process->wait_options
					      : (uint32_t)options));
		serial_print("\n");
	);

	for (;;) {
		const int active_opts = (current_process->mode == USER_MODE)
			? current_process->wait_options
			: options;
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
			process_fase50_trace_proc("process_wait-found-zombie", zombie);

			if (status &&
			    validate_userspace_buffer(status, sizeof(int)) != 0)
			{
				process_irq_restore(irq_flags);
				return -EFAULT;
			}

			status_val = process_child_wait_status_word(zombie);
			reaped_pid = zombie->task.pid;
			process_irq_restore(irq_flags);

			FASE40_D_AUDIT_LOG(
				serial_print("[FASE40_D_AUDIT][WAIT_REAP] parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" child=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" tag=wait\n");
			);
			fase_audit_reap_zombie(zombie, current_process->task.pid, "wait");
			process_fase50_trace_proc("process_wait-after-reap", current_process);
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
			current_process->wait_blocked = 0;
			current_process->wait_target_pid = 0;
			current_process->wait_options = 0;
			current_process->wait_resume_child_pid = 0;
			current_process->syscall_resume_rax = 0;
			current_process->coop_resched_resume = 0;
			current_process->task.rax = (uint64_t)(uint32_t)reaped_pid;

			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" child=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" status=");
				serial_print_hex64((uint64_t)(uint32_t)status_val);
				serial_print("\n");
			}

			FASE40_D_AUDIT_LOG(
				serial_print("[FASE40_D_AUDIT][WAIT_RETURN] parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" pid=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print(" status=");
				serial_print_hex32((uint32_t)status_val);
				serial_print(" rax=");
				serial_print_hex32((uint32_t)reaped_pid);
				serial_print("\n");
			);

			return reaped_pid;
		}

		if (!found_child)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				serial_print("[WAIT4_WNOHANG_AUDIT] path=echild parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" target=");
				serial_print_hex32((uint32_t)pid);
				serial_print(" ret=ECHILD\n");
			}
			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" ret=ECHILD\n");
			}
			process_reset_blocked_syscall_state(current_process);
			return -ECHILD;
		}

		if (active_opts & WNOHANG)
		{
			process_irq_restore(irq_flags);
			if (IR0_DEBUG_WAIT)
			{
				serial_print("[WAIT4_WNOHANG_AUDIT] path=wnohang_alive parent=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" target=");
				serial_print_hex32((uint32_t)pid);
				serial_print(" ret=0 status_write=no\n");
			}
			if (IR0_DEBUG_PROC)
			{
				serial_print("[FASE41][WAIT] pid=");
				serial_print_hex32((uint32_t)current_process->task.pid);
				serial_print(" ret=WNOHANG\n");
			}
			process_reset_blocked_syscall_state(current_process);
			return 0;
		}

		/*
		 * Arm wait contract before dropping irq: child exit wake is ignored
		 * until wait_blocked is set (process_wait_wake_blocked_parent).
		 */
		if (current_process->mode == USER_MODE)
		{
			wait_exit_audit_process_wait_block(pid, status);
			current_process->wait_status_ptr = status;
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
			current_process->coop_resched_resume = 0;
			current_process->syscall_resume_rax = 0;
			current_process->task.rax = 0;
			process_arm_blocked_syscall_resume(current_process, 0);
			process_arm_kernel_syscall_sleep(current_process);
			wait_exit_audit_classify_user_frame("parent-after-wait-arm",
							    current_process);
		}
		else
		{
			current_process->wait_blocked = 1;
			current_process->wait_target_pid = pid;
			current_process->wait_options = active_opts;
			current_process->wait_resume_child_pid = 0;
		}

		zombie = NULL;
		for (p = process_list; p; p = p->next)
		{
			if (p->ppid != current_process->task.pid)
				continue;
			if (!any_child && p->task.pid != pid)
				continue;
			if (p->state == PROCESS_ZOMBIE)
			{
				zombie = p;
				break;
			}
		}
		process_irq_restore(irq_flags);
		if (zombie)
			continue;

		/*
		 * Child exit may have woken us after irq_restore; do not clobber
		 * READY with BLOCKED (missed wake → stuck with a zombie).
		 */
		if (current_process->state == PROCESS_READY)
			continue;

		FASE40_D_AUDIT_LOG(
			serial_print("[FASE40_D_AUDIT][WAIT_BLOCK] parent=");
			serial_print_hex32((uint32_t)current_process->task.pid);
			serial_print(" target=");
			serial_print_hex32((uint32_t)pid);
			serial_print("\n");
		);

		current_process->state = PROCESS_BLOCKED;
		while (current_process->state == PROCESS_BLOCKED)
		{
			ir0_clock_wait_service_runqueue();
			if (current_process->state != PROCESS_BLOCKED)
				break;
		}
		/*
		 * Stay on kernel CS until process_wait returns; syscall_dispatch
		 * restores user segments at sysret. Restoring here while still
		 * inside process_wait lets switch_context user-iret with the
		 * block-time rax=0 placeholder (wait4_block_reap flake).
		 */
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
		process->fd_table[i].is_pipe = false;
		process->fd_table[i].is_socket = false;
		process->fd_table[i].is_devfs = false;
		process->fd_table[i].is_pseudo = false;
		process->fd_table[i].dev_device_id = 0;
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
