/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include "process.h"

void cfs_add_process(process_t *proc);
void cfs_remove_process(process_t *proc);
void cfs_schedule_next(void);
