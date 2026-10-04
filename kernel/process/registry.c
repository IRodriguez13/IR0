/* SPDX-License-Identifier: GPL-3.0-only */
/* Single owner for the architecture-neutral process registry state. */

#include "process_internal.h"

process_t *current_process;
process_t *process_list;
