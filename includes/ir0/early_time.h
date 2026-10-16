/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>
#include <ir0/syscall_table.h>

#define IR0_EARLY_TIMEX_BYTES 208U
#define IR0_EARLY_ADJ_OFFSET 0x0001U
#define IR0_EARLY_ADJ_SETOFFSET 0x0100U
#define IR0_EARLY_ADJ_NANO 0x2000U
#define IR0_EARLY_ADJ_OFFSET_SINGLESHOT 0x8001U
#define IR0_EARLY_ADJ_KNOWN \
	(IR0_EARLY_ADJ_OFFSET | IR0_EARLY_ADJ_SETOFFSET | IR0_EARLY_ADJ_NANO | \
	 IR0_EARLY_ADJ_OFFSET_SINGLESHOT)
#define IR0_EARLY_TIME_OK 0

extern const struct syscall_context_provider early_time_provider;
int early_time_smoke_ok(void);
int64_t early_time_realtime_offset(void);
int64_t early_time_set_realtime(void *opaque, int64_t sec, int64_t nsec);
