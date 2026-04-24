/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel - Scheduler backend API
 *
 * For now RR is the only implementation. This file centralizes callsites so
 * policy selection can evolve without touching core subsystems.
 */

#include "scheduler_api.h"
#include <config.h>
#if CONFIG_SCHEDULER_POLICY == 1
#include "cfs_sched.h"
#elif CONFIG_SCHEDULER_POLICY == 2
#include "priority_sched.h"
#else
#include "rr_sched.h"
#endif
void sched_add_process(process_t *proc)
{
#if CONFIG_SCHEDULER_POLICY == 1
    cfs_add_process(proc);
#elif CONFIG_SCHEDULER_POLICY == 2
    priority_add_process(proc);
#else
    rr_add_process(proc);
#endif
}

void sched_remove_process(process_t *proc)
{
#if CONFIG_SCHEDULER_POLICY == 1
    cfs_remove_process(proc);
#elif CONFIG_SCHEDULER_POLICY == 2
    priority_remove_process(proc);
#else
    rr_remove_process(proc);
#endif
}

void sched_schedule_next(void)
{
#if CONFIG_SCHEDULER_POLICY == 1
    cfs_schedule_next();
#elif CONFIG_SCHEDULER_POLICY == 2
    priority_schedule_next();
#else
    rr_schedule_next();
#endif
}

const char *sched_active_policy_name(void)
{
#if defined(CONFIG_SCHEDULER_POLICY)
    switch (CONFIG_SCHEDULER_POLICY)
    {
    case 0:
        return "round_robin";
    case 1:
        return "cfs";
    case 2:
        return "priority";
    default:
        return "round_robin";
    }
#else
    return "round_robin";
#endif
}
