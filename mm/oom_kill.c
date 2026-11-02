/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026 Iván Rodriguez
 *
 * File: oom_kill.c
 * Description: Linux-style OOM killer integration: collect victims from the
 *              process registry, pick one with the pure policy in oom.c and
 *              deliver SIGKILL before the allocator retries.
 *
 * Linux reference: mm/oom_kill.c (out_of_memory / select_bad_process /
 * oom_kill_process) and __alloc_pages_slowpath, which runs the killer only
 * after direct reclaim has failed. IR0 has no swap or page cache to reclaim,
 * so the hook fires directly from the failing user allocation.
 */

#include <ir0/oom.h>
#include <ir0/oops.h>
#include <ir0/process.h>
#include <ir0/pmm.h>
#include <ir0/signals.h>
#include <ir0/cpu.h>
#include <ir0/ktm/klog.h>
#include <string.h>
#include <stdint.h>

static uint64_t oom_kills;
/*
 * Re-entrancy guard: the kill path frees address spaces and can touch the
 * allocator again. A nested OOM event must not run a second victim election.
 */
static volatile int oom_scan_active;

static void oom_candidate_project(const process_t *process,
                                  const process_t *origin,
                                  struct ir0_oom_candidate *out)
{
    uint64_t resident = 0;
    uint64_t tables = 0;

    memset(out, 0, sizeof(*out));
    out->pid = process->task.pid;
    if (process->mode == USER_MODE)
        out->flags |= IR0_OOM_CANDIDATE_USER;
    if (process->lifecycle == PROCESS_LIFECYCLE_ALIVE)
        out->flags |= IR0_OOM_CANDIDATE_ALIVE;
    if (process->mm)
    {
        out->flags |= IR0_OOM_CANDIDATE_HAS_MM;
        mm_count_user_pages(process->mm, &resident, &tables);
    }
    if (process->lifecycle != PROCESS_LIFECYCLE_ALIVE ||
        process->state == PROCESS_ZOMBIE)
        out->flags |= IR0_OOM_CANDIDATE_SKIP;
    if (process == origin)
        out->flags |= IR0_OOM_CANDIDATE_ORIGIN;

    out->resident_pages = resident;
    out->page_table_pages = tables;
    out->score_adj = process->oom_score_adj;
}

uint64_t oom_kill_count(void)
{
    return oom_kills;
}

int oom_kill_process(struct process *victim, const char *reason)
{
    int pid;

    if (!victim)
        return -EINVAL;

    pid = (int)victim->task.pid;
    if (pid <= 1)
        return -EPERM;

    /*
     * Linux logs the victim before signalling so a hang inside teardown still
     * leaves the audit trail on the serial console.
     */
    klog_notice_fmt("OOM",
                    "[OOM] kill pid=%d comm=%s reason=%s",
                    pid, victim->comm[0] ? victim->comm : "(none)",
                    reason ? reason : "unknown");
    oom_kills++;

    /*
     * send_signal() owns default-fatal delivery: for a foreign task it tears
     * the address space down and leaves a zombie for the parent; for the
     * current task it never returns from process_exit().
     */
    if (send_signal(pid, SIGKILL) != 0)
        return -ESRCH;
    return 0;
}

int oom_out_of_memory(struct process *origin, const char *reason)
{
    struct ir0_oom_candidate candidate;
    struct ir0_oom_candidate best;
    process_t *process;
    unsigned long flags;
    uint64_t total;
    int found = 0;
    int pid;

    if (oom_scan_active)
        return -EBUSY;
    oom_scan_active = 1;

    total = (uint64_t)pmm_total_frames();
    flags = irq_save();
    for (process = process_list; process; process = process->next)
    {
        oom_candidate_project(process, origin, &candidate);
        if (!oom_candidate_better(&candidate, found ? &best : NULL, total))
            continue;
        best = candidate;
        found = 1;
    }
    irq_restore(flags);
    oom_scan_active = 0;

    if (!found)
    {
        klog_notice_fmt("OOM",
                        "[OOM] no eligible victim (origin=%d reason=%s)",
                        origin ? (int)origin->task.pid : -1,
                        reason ? reason : "unknown");
        return -ESRCH;
    }

    /*
     * Re-resolve by PID instead of keeping the pointer: the election ran with
     * IRQs disabled, and the victim may have been reaped while we unlocked.
     */
    pid = (int)best.pid;
    process = process_find_by_pid(pid);
    if (!process)
        return -ESRCH;

    return oom_kill_process(process, reason);
}

int oom_handle_alloc_failure(struct process *current, const char *site)
{
    if (!current || current->mode != USER_MODE)
        return -ENOMEM;
    if (ir0_panic_in_progress())
        return -ENOMEM;

    return oom_out_of_memory(current, site);
}
