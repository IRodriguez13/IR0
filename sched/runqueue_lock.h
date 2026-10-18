/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/*
 * Scheduler ownership boundary.  UP serializes runqueue mutation against
 * timer IRQ scheduling.  The SMP backend will add a real runqueue lock here
 * without exposing its implementation to policy backends.
 */
uint64_t sched_runqueue_lock(void);
void sched_runqueue_unlock(uint64_t irq_flags);
