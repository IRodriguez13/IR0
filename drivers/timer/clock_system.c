/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: clock_system.c
 * Description: Kernel timer managment subsystem
 */

#include <stdint.h>
#include <string.h>
#include <ir0/vga.h>
#include <ir0/types.h>
#include <ir0/kmem.h>
#include <kernel/rr_sched.h>
#include "pit/pit.h"
#include "clock_system.h"

/* Global clock state */
static clock_state_t clock_state;

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
    clock_state.timer_frequency = 1000; /* 1kHz default */
    clock_state.timer_ticks_per_second = 1000;
    
    /* Initialize time tracking */
    clock_state.boot_time = 0;
    clock_state.current_time = 0;
    clock_state.timezone_offset = 0;
    
    /* Initialize timer sources */
    clock_state.pit_enabled = 0;
    clock_state.hpet_enabled = 0;
    clock_state.lapic_enabled = 0;
    clock_state.active_timer = CLOCK_TIMER_NONE;
    
    /* Initialize scheduler integration (10 ticks per quantum for 10ms time slice at 1kHz) */
    clock_state.scheduler_tick_counter = 0;
    clock_state.scheduler_ticks_per_quantum = 10;
    
    /* Initialize alarm system */
    clock_state.alarms = NULL;
    clock_state.alarm_count = 0;
    
    /* Initialize PIT */
    extern void init_PIT(uint32_t frequency);
    init_PIT(1000); /* 1kHz timer */
    clock_state.pit_enabled = 1;
    clock_state.active_timer = CLOCK_TIMER_PIT;
    
    /* Mark as initialized */
    clock_state.initialized = 1;
    print_success("Clock system initialized successfully with PIT\n");
    return 0;
}

/* Wrapper function for compatibility */
void init_clock(void)
{
    clock_system_init();
}

/* Timer detection - prioritize higher precision timers */
clock_timer_t detect_best_clock(void)
{
    /* Try to detect best available timer in order of preference:
     * 1. LAPIC timer (best precision, per-CPU, ~1MHz)
     * 2. HPET (high precision, system-wide, sub-microsecond)
     * 3. PIT (fallback, always available, ~1.19MHz but less precise)
     */
    
    /* Check if LAPIC timer is available */
    extern int lapic_available(void);
    if (lapic_available())
    {
        clock_state.lapic_enabled = 1;
        clock_state.active_timer = CLOCK_TIMER_LAPIC;
        return CLOCK_TIMER_LAPIC;
    }
    
    /* Check if HPET is available by trying to find HPET table */
    extern void *find_hpet_table(void);
    void *hpet_table = find_hpet_table();
    if (hpet_table != NULL)
    {
        clock_state.hpet_enabled = 1;
        clock_state.active_timer = CLOCK_TIMER_HPET;
        return CLOCK_TIMER_HPET;
    }
    
    /* Fall back to PIT (always available on x86) */
    clock_state.pit_enabled = 1;
    clock_state.active_timer = CLOCK_TIMER_PIT;
    return CLOCK_TIMER_PIT;
}

clock_timer_t get_current_timer_type(void)
{
    return clock_state.active_timer;
}

/* Time functions */
uint64_t clock_get_uptime_seconds(void)
{
    return clock_state.uptime_seconds;
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

int clock_set_current_time(time_t time)
{
    clock_state.current_time = time;
    return 0;
}

/* Timezone functions */
int clock_set_timezone_offset(int32_t offset_seconds)
{
    clock_state.timezone_offset = offset_seconds;
    return 0;
}

int32_t clock_get_timezone_offset(void)
{
    return clock_state.timezone_offset;
}

/* Timer information */
clock_timer_t clock_get_active_timer(void)
{
    return clock_state.active_timer;
}

uint32_t clock_get_timer_frequency(void)
{
    return clock_state.timer_frequency;
}

/* Sleep functions */
int clock_sleep(uint32_t milliseconds)
{
    /* Simple implementation using ticks */
    uint32_t ticks = (milliseconds * clock_state.timer_frequency) / 1000;
    return clock_sleep_ticks(ticks);
}

int clock_sleep_ticks(uint32_t ticks)
{
    uint32_t start_ticks = clock_state.tick_count;
    while ((clock_state.tick_count - start_ticks) < ticks) 
    {
        /* Wait for ticks to increment */
        __asm__ volatile("hlt");
    }
    return 0;
}

/* Alarm functions */
int clock_set_alarm(uint32_t seconds, clock_alarm_callback_t callback, void *data)
{
    if (!callback || seconds == 0)
    {
        return -1;
    }
    
    /* Calculate trigger time (in milliseconds) */
    uint64_t current_time = clock_get_uptime_milliseconds();
    uint64_t trigger_time = current_time + (seconds * 1000);
    
    /* Allocate alarm structure */
    clock_alarm_t *alarm = kmalloc(sizeof(clock_alarm_t));
    if (!alarm)
    {
        return -1;
    }
    
    /* Initialize alarm */
    alarm->trigger_time = trigger_time;
    alarm->callback = callback;
    alarm->data = data;
    alarm->active = 1;
    
    /* Add to alarm list (sorted by trigger time) */
    clock_alarm_t **current = &clock_state.alarms;
    while (*current && (*current)->trigger_time < trigger_time)
    {
        current = &(*current)->next;
    }
    
    alarm->next = *current;
    *current = alarm;
    clock_state.alarm_count++;
    
    return 0;
}

/* Internal function to check and fire alarms */
static void clock_check_alarms(void)
{
    if (!clock_state.alarms)
    {
        return;
    }
    
    uint64_t current_time = clock_get_uptime_milliseconds();
    clock_alarm_t *current = clock_state.alarms;
    clock_alarm_t *prev = NULL;
    
    while (current)
    {
        if (!current->active)
        {
            /* Remove inactive alarm */
            clock_alarm_t *next = current->next;
            if (prev)
            {
                prev->next = next;
            }
            else
            {
                clock_state.alarms = next;
            }
            kfree(current);
            current = next;
            clock_state.alarm_count--;
            continue;
        }
        
        if (current->trigger_time <= current_time)
        {
            /* Fire alarm */
            clock_alarm_callback_t callback = current->callback;
            void *data = current->data;
            
            /* Remove alarm before calling callback (to allow re-adding) */
            clock_alarm_t *next = current->next;
            if (prev)
            {
                prev->next = next;
            }
            else
            {
                clock_state.alarms = next;
            }
            kfree(current);
            current = next;
            clock_state.alarm_count--;
            
            /* Call callback */
            if (callback)
            {
                callback(data);
            }
        }
        else
        {
            /* Alarms are sorted, so we can stop here */
            break;
        }
    }
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
    stats->timezone_offset = clock_state.timezone_offset;
    
    return 0;
}

void clock_print_stats(void)
{
    clock_stats_t stats;
    if (clock_get_stats(&stats) != 0) 
    {
        return;
    }
    
    print("=== Clock System Statistics ===\n");
    print("Initialized: ");
    print(stats.initialized ? "Yes" : "No");
    print("\n");
    
    print("Active Timer: ");
    switch (stats.active_timer) 
    {
        case CLOCK_TIMER_PIT:
            print("PIT");
            break;
        case CLOCK_TIMER_HPET:
            print("HPET");
            break;
        case CLOCK_TIMER_LAPIC:
            print("LAPIC");
            break;
        default:
            print("None");
            break;
    }
    print("\n");
    
    print("Timer Frequency: ");
    print_uint32(stats.timer_frequency);
    print(" Hz\n");
    
    print("Tick Count: ");
    print_uint64(stats.tick_count);
    print("\n");
    
    print("Uptime: ");
    print_uint64(stats.uptime_seconds);
    print(" seconds ");
    print_uint32(stats.uptime_milliseconds);
    print(" milliseconds\n");
    
    print("Current Time: ");
    print_uint64(stats.current_time);
    print(" seconds since epoch\n");
    
    print("Timezone Offset: ");
    print_int32(stats.timezone_offset);
    print(" seconds\n");
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
    
    /* Update uptime */
    clock_state.uptime_milliseconds++;
    if (clock_state.uptime_milliseconds >= 1000) 
    {
        clock_state.uptime_milliseconds = 0;
        clock_state.uptime_seconds++;
        
        /* Update current time (increment by 1 second when milliseconds wrap) */
        clock_state.current_time++;
    }
    
    /* Check and fire alarms */
    clock_check_alarms();
    
    /* Scheduler integration: call scheduler every N ticks */
    clock_state.scheduler_tick_counter++;
    if (clock_state.scheduler_tick_counter >= clock_state.scheduler_ticks_per_quantum)
    {
        clock_state.scheduler_tick_counter = 0;
        /* Call scheduler to switch processes */
        rr_schedule_next();
    }
}

/* SYSTEM TIME FUNCTIONS */

uint64_t get_system_time(void)
{
    if (!clock_state.initialized) 
    {
        return 0;
    }
    
    /* Return uptime in milliseconds (used by /proc/uptime and other subsystems) */
    return clock_get_uptime_milliseconds();
}

/* Additional utility functions */

/* Get time since boot in seconds */
uint64_t clock_get_boot_time(void)
{
    return clock_state.boot_time;
}

/* Set scheduler quantum (ticks per quantum) */
int clock_set_scheduler_quantum(uint32_t ticks)
{
    if (ticks == 0)
    {
        return -1;
    }
    clock_state.scheduler_ticks_per_quantum = ticks;
    return 0;
}

/* Get scheduler quantum */
uint32_t clock_get_scheduler_quantum(void)
{
    return clock_state.scheduler_ticks_per_quantum;
}

/* Cancel all alarms */
void clock_cancel_all_alarms(void)
{
    clock_alarm_t *current = clock_state.alarms;
    while (current)
    {
        clock_alarm_t *next = current->next;
        kfree(current);
        current = next;
    }
    clock_state.alarms = NULL;
    clock_state.alarm_count = 0;
}


