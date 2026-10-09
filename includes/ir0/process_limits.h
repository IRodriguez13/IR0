/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

uint32_t process_limit_get(void);
int process_limit_set(uint32_t limit);
int process_live_count(void);
