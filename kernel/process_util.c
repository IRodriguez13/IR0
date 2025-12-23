// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: process_util.c
 * Description: Process utility functions (lookup, etc.)
 */

#include "process.h"
#include <drivers/serial/serial.h>

/* External process list from process.c */
extern process_t *process_list;

/**
 * process_find_by_pid - Find process by PID
 * @pid: Process ID to find
 *
 * Returns: Pointer to process, or NULL if not found
 */
process_t *process_find_by_pid(pid_t pid)
{
    process_t *proc = process_list;
    
    while (proc)
    {
        if (proc->task.pid == pid)
        {
            return proc;
        }
        proc = proc->next;
    }
    
    return NULL; /* Not found */
}
