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
	uint64_t tick_count;
	uint64_t uptime_seconds;
	uint32_t uptime_milliseconds;
	uint32_t time_resolution;
	uint32_t timer_frequency;
	uint32_t timer_ticks_per_second;
	time_t boot_time;
	time_t current_time;
	int32_t timezone_offset;
	int pit_enabled;
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
	struct clock_alarm *alarms;
	uint32_t alarm_count;
} clock_state_t;

typedef void (*clock_alarm_callback_t)(void *data);

typedef struct clock_alarm
{
	uint64_t trigger_time;
	clock_alarm_callback_t callback;
	void *data;
	struct clock_alarm *next;
	int active;
} clock_alarm_t;

typedef struct
{
	int initialized;
	clock_timer_t active_timer;
	uint32_t timer_frequency;
	uint64_t tick_count;
	uint64_t uptime_seconds;
	uint32_t uptime_milliseconds;
	time_t current_time;
	int32_t timezone_offset;
} clock_stats_t;

int clock_system_init(void);
void init_clock(void);

clock_timer_t detect_best_clock(void);
clock_timer_t get_current_timer_type(void);

/* Non-zero once PIT/timer path marked clock_state.initialized. */
int clock_is_ready(void);

uint64_t clock_get_uptime_seconds(void);
uint64_t clock_get_uptime_milliseconds(void);
uint64_t clock_get_tick_count(void);
time_t clock_get_current_time(void);
int clock_set_current_time(time_t time);

int clock_set_timezone_offset(int32_t offset_seconds);
int32_t clock_get_timezone_offset(void);

clock_timer_t clock_get_active_timer(void);
uint32_t clock_get_timer_frequency(void);

int clock_sleep(uint32_t milliseconds);
int clock_sleep_ticks(uint32_t ticks);

int clock_set_alarm(uint32_t seconds, clock_alarm_callback_t callback, void *data);
void clock_cancel_all_alarms(void);

int clock_set_scheduler_quantum(uint32_t ticks);
uint32_t clock_get_scheduler_quantum(void);
int clock_take_sched_resched_pending(void);

/* Request cooperative reschedule at user-return (e.g. after waking a blocked task). */
void clock_request_sched_resched(void);

int clock_get_stats(clock_stats_t *stats);
void clock_print_stats(void);

void clock_tick(void);

uint64_t get_system_time(void);
uint64_t clock_get_boot_time(void);
