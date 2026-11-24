/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * File: loadavg.h
 * Description: Load average and run counts (Linux include/linux/sched/loadavg.h).
 */

#pragma once

#define FSHIFT 11
#define FIXED_1 (1UL << FSHIFT)
#define LOAD_INT(x) ((x) >> FSHIFT)
#define LOAD_FRAC(x) LOAD_INT(((x) & (FIXED_1 - 1)) * 100)

/* Fold the run count into the averages every LOAD_FREQ; once per tick. */
void calc_global_load_tick(void);
/* The three averages in FSHIFT fixed point, plus @offset, << @shift. */
void get_avenrun(unsigned long *loads, unsigned long offset, int shift);
/* Runnable tasks (on a CPU or waiting for one), the idle task excluded. */
unsigned int nr_running(void);
/* Live tasks, threads included. */
unsigned int nr_threads(void);
