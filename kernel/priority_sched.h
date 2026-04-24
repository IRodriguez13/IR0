/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "process.h"

void priority_add_process(process_t *proc);
void priority_remove_process(process_t *proc);
void priority_schedule_next(void);
