/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clock_system.c
 * Description: Kernel timer managment subsystem
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <stdint.h>
#include <string.h>
#include <config.h>
#include <ir0/cpu_info.h>
#include <ir0/types.h>
#include <ir0/sched.h>
#include <ir0/clock_wait.h>
#include <ir0/delay.h>
#include <ir0/poll.h>
#include <ir0/ktm/klog.h>
#include <ir0/rtc.h>
#include <ir0/process.h>
#include <ir0/clock.h>
#include <ir0/loadavg.h>

/* Clock state; private to the clock core. */
typedef struct
{
	int initialized;
	uint64_t tick_count;
	uint64_t uptime_seconds;
	uint32_t uptime_milliseconds;
	uint32_t time_resolution;
	uint32_t timer_frequency;
	uint32_t timer_ticks_per_second;
	time_t boot_time;
	time_t current_time;
	int hpet_enabled;
	int lapic_enabled;
	clock_timer_t active_timer;
	uint32_t pit_divisor;
	uint32_t pit_frequency;
	uint64_t hpet_frequency;
	uint64_t hpet_ticks_per_ms;
	uint32_t lapic_frequency;
	uint32_t lapic_ticks_per_ms;
	uint32_t scheduler_tick_counter;
	uint32_t scheduler_ticks_per_quantum;
	uint8_t sched_resched_pending;
} clock_state_t;

static clock_state_t clock_state;

/* Idle CPU time (ms spent in the idle task). UP today; sum per-CPU later. */
static uint64_t clock_idle_ms;
static uint64_t clock_cpu_user;
static uint64_t clock_cpu_system;
static uint64_t clock_cpu_idle;

static int clock_realtime_ok;

static int clock_comm_is_idle(const char *comm)
{
	if (!comm)
		return 0;
	return (comm[0] == 'i' && comm[1] == 'd' && comm[2] == 'l' &&
		comm[3] == 'e' && comm[4] == '\0');
}

static void clock_try_set_realtime_from_rtc(void)
{
	rtc_time_t rt;
	time_t epoch;

	if (rtc_init() != 0)
	{
		clock_realtime_ok = 0;
		return;
	}
	if (rtc_read_time(&rt) != 0)
	{
		clock_realtime_ok = 0;
		return;
	}
	epoch = rtc_fields_to_unix(&rt);
	/*
	 * Accept any civil year ≥ 1970 (including QEMU default 1970-01-01).
	 * Reject only impossible conversions (rtc_fields_to_unix returns 0 with
	 * zeroed day/month — still a valid epoch for 1970-01-01 00:00:00).
	 */
	if (rt.month == 0 || rt.day == 0)
	{
		clock_realtime_ok = 0;
		return;
	}
	clock_state.boot_time = epoch;
	clock_state.current_time = epoch;
	clock_realtime_ok = 1;
}

/*                          MAIN CLOCK SYSTEM FUNCTIONS */

int clock_system_init(void)
{    
    /* Initialize clock state */
    memset(&clock_state, 0, sizeof(clock_state_t));
    clock_state.initialized = 0;
    clock_state.tick_count = 0;
    clock_state.uptime_seconds = 0;
    clock_state.uptime_milliseconds = 0;
    clock_state.time_resolution = CLOCK_RESOLUTION_MS;
    clock_state.timer_frequency = CONFIG_TICK_RATE_HZ;
    clock_state.timer_ticks_per_second = CONFIG_TICK_RATE_HZ;
    
    /* Initialize time tracking */
    clock_state.boot_time = 0;
    clock_state.current_time = 0;
    
    /* Initialize timer sources */
    clock_state.hpet_enabled = 0;
    clock_state.lapic_enabled = 0;
    clock_state.active_timer = CLOCK_TIMER_NONE;
    
    /* Initialize scheduler integration for ~10ms quantum. */
    clock_state.scheduler_tick_counter = 0;
    clock_state.scheduler_ticks_per_quantum = (CONFIG_TICK_RATE_HZ / 100);
    if (clock_state.scheduler_ticks_per_quantum == 0)
        clock_state.scheduler_ticks_per_quantum = 1;

    clock_state.active_timer = clock_timer_init();

    /* Wall clock: realtime_boot_epoch = RTC_UTC; then += monotonic elapsed. */
    clock_try_set_realtime_from_rtc();
    if (clock_realtime_ok)
	    klog_notice("CLOCK", "CLOCK_REALTIME from CMOS RTC (UTC)");
    else
	    klog_notice("CLOCK", "CLOCK_REALTIME unavailable (no usable RTC)");

    /* Mark as initialized */
    clock_state.initialized = 1;
    klog_notice("CLOCK", "Clock system initialized");
    return 0;
}

int clock_is_ready(void)
{
	return clock_state.initialized ? 1 : 0;
}

uint64_t clock_get_uptime_milliseconds(void)
{
    return clock_state.uptime_seconds * 1000 + clock_state.uptime_milliseconds;
}

uint64_t clock_get_tick_count(void)
{
    return clock_state.tick_count;
}

time_t clock_get_current_time(void)
{
    return clock_state.current_time;
}

int clock_realtime_available(void)
{
	return clock_realtime_ok;
}

uint64_t clock_get_idle_milliseconds(void)
{
	return clock_idle_ms;
}

void clock_get_cpu_accounting(uint64_t *user, uint64_t *system, uint64_t *idle)
{
	if (user)
		*user = clock_cpu_user;
	if (system)
		*system = clock_cpu_system;
	if (idle)
		*idle = clock_cpu_idle;
}

uint32_t clock_get_timer_frequency(void)
{
    return clock_state.timer_frequency;
}

/*
 * clock_sleep - Sleep at least @milliseconds (Linux msleep_interruptible).
 *
 * A task sleeps in the clock wait until the timer wakes it; a pending
 * signal ends the sleep early with -EINTR. Before the first task exists
 * there is nothing to sleep in, so it is a bounded udelay().
 */
int clock_sleep(uint32_t milliseconds)
{
	if (!current_process)
	{
		udelay((unsigned long)milliseconds * 1000UL);
		return 0;
	}
	return ir0_clock_wait_block_until(clock_get_uptime_milliseconds() +
					  milliseconds);
}

/* Statistics */
int clock_get_stats(clock_stats_t *stats)
{
    if (!stats) 
    {
        return -1;
    }
    
    memset(stats, 0, sizeof(clock_stats_t));
    
    stats->initialized = clock_state.initialized;
    stats->active_timer = clock_state.active_timer;
    stats->timer_frequency = clock_state.timer_frequency;
    stats->tick_count = clock_state.tick_count;
    stats->uptime_seconds = clock_state.uptime_seconds;
    stats->uptime_milliseconds = clock_state.uptime_milliseconds;
    stats->current_time = clock_state.current_time;
    
    return 0;
}

/* Tick handler (called from interrupt) */
void clock_tick(void)
{
    if (!clock_state.initialized) 
    {
        return;
    }
    
    /* Increment tick count */
    clock_state.tick_count++;
    
	/*
	 * UP CPU accounting: attribute each tick to whoever is running now.
	 * Do not infer busy from other READY tasks — that made /proc/stat idle
	 * read 0% on a quiet desktop while the idle task was actually running.
	 */
	{
		process_t *cur = current_process;

		if (!cur || clock_comm_is_idle(cur->comm))
		{
			clock_cpu_idle++;
			clock_idle_ms++;
		}
		else if (cur->mode == USER_MODE)
		{
			cur->utime++;
			clock_cpu_user++;
		}
		else
		{
			cur->stime++;
			clock_cpu_system++;
		}
	}

    /* Update uptime */
    clock_state.uptime_milliseconds++;
    if (clock_state.uptime_milliseconds >= 1000) 
    {
        clock_state.uptime_milliseconds = 0;
        clock_state.uptime_seconds++;
        
        /* Wall clock tracks monotonic once RTC epoch was established. */
	if (clock_realtime_ok)
		clock_state.current_time++;
    }
    calc_global_load_tick();

    /* POSIX ITIMER_REAL → SIGALRM (setitimer(2), alarm(2)). */
    process_itimer_tick(clock_get_uptime_milliseconds());

    /* Timed waits (nanosleep, poll timeout) and poll fd timeouts. */
    ir0_clock_wait_fire_due(clock_get_uptime_milliseconds());
    (void)poll_wake_check_nosched();

    /* Scheduler tick accounting (preempt deferred — see sched/core.c). */
    clock_state.scheduler_tick_counter++;
    if (clock_state.scheduler_tick_counter >= clock_state.scheduler_ticks_per_quantum)
    {
        clock_state.scheduler_tick_counter = 0;
        clock_state.sched_resched_pending = 1;
    }
    
    /*
     * Do not poll network stacks from IRQ0 context.
     * Networking is polled from the main loop where IRQ latency is unaffected.
     */
}

/* Wall-clock epoch (seconds) captured at boot. */
uint64_t clock_get_boot_time(void)
{
    return clock_state.boot_time;
}

int clock_take_sched_resched_pending(void)
{
    int pending;

    pending = clock_state.sched_resched_pending ? 1 : 0;
    clock_state.sched_resched_pending = 0;
    return pending;
}

void clock_request_sched_resched(void)
{
    clock_state.sched_resched_pending = 1;
}

