/* SPDX-License-Identifier: GPL-3.0-only */
/* Scheduler backend selection stays private to sched/. */

#include "backend.h"
#include <config.h>

#if CONFIG_SCHEDULER_POLICY == 2 && !defined(IR0_SCHED_EARLY_RR)
#include "prio/backend.h"
#else
#include "rr/backend.h"
#endif

const struct ir0_sched_ops *sched_backend_active(void)
{
#if CONFIG_SCHEDULER_POLICY == 2 && !defined(IR0_SCHED_EARLY_RR)
	return &sched_prio_ops;
#else
	return &sched_rr_ops;
#endif
}
