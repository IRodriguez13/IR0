/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clock.h
 * Description: Kernel clock/timer facade (portable API)
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stdint.h>
#include <ir0/types.h>

typedef enum
{
	CLOCK_TIMER_NONE = 0,
	CLOCK_TIMER_PIT,
	CLOCK_TIMER_HPET,
	CLOCK_TIMER_LAPIC,
	CLOCK_TIMER_RTC
} clock_timer_t;

#define CLOCK_RESOLUTION_MS 1



typedef struct
{
	int initialized;
	clock_timer_t active_timer;
	uint32_t timer_frequency;
	uint64_t tick_count;
	uint64_t uptime_seconds;
	uint32_t uptime_milliseconds;
	time_t current_time;
} clock_stats_t;

int clock_system_init(void);

/*
 * Start the platform's periodic tick at CONFIG_TICK_RATE_HZ and return the
 * timer that drives it. The platform timer driver implements it (PC: the
 * PIT; arm64: the generic timer).
 */
clock_timer_t clock_timer_init(void);


/* Non-zero once PIT/timer path marked clock_state.initialized. */
int clock_is_ready(void);

uint64_t clock_get_uptime_milliseconds(void);
uint64_t clock_get_tick_count(void);
time_t clock_get_current_time(void);

/* Non-zero when CLOCK_REALTIME is backed by the RTC wall epoch. */
int clock_realtime_available(void);
uint64_t clock_get_idle_milliseconds(void);

/*
 * Aggregate CPU time since boot in scheduler ticks (jiffies_to_clock_t()
 * gives the USER_HZ figure /proc/stat prints).
 * user + system + idle <= clock_get_tick_count() (UP).
 */
void clock_get_cpu_accounting(uint64_t *user, uint64_t *system, uint64_t *idle);

uint32_t clock_get_timer_frequency(void);

/* Linux USER_HZ: the unit of every clock_t in the ABI (/proc, times(2)). */
#define USER_HZ 100

/* Scheduler ticks (CONFIG_TICK_RATE_HZ) to clock_t and to microseconds. */
uint64_t jiffies_to_clock_t(uint64_t ticks);
uint64_t jiffies_to_usecs(uint64_t ticks);

/*
 * Sleep at least @milliseconds; 0, or -EINTR when a signal ended it.
 * Without a current task (early boot) it is a bounded udelay().
 */
int clock_sleep(uint32_t milliseconds);


int clock_take_sched_resched_pending(void);

/* Request cooperative reschedule at user-return (e.g. after waking a blocked task). */
void clock_request_sched_resched(void);

int clock_get_stats(clock_stats_t *stats);

void clock_tick(void);

uint64_t clock_get_boot_time(void);

/* /proc/stat aggregate counters (Linux proc(5) intr/ctxt lines). */
void clock_note_irq(void);
void clock_note_context_switch(void);
uint64_t clock_get_irq_count(void);
uint64_t clock_get_context_switch_count(void);
