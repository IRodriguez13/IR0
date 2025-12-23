// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: signals.c
 * Description: Basic signal implementation
 */

#include "signals.h"
#include <kernel/process.h>
#include <drivers/serial/serial.h>
#include <config.h>

/**
 * send_signal - Send signal to process
 */
int send_signal(int pid, int signal)
{
    /* Validate signal number */
    if (signal < 0 || signal >= _NSIG)
    {
        return -1;
    }

    /* Find target process - for now, stub (no process list walking) */
    /* TODO: Implement proper process lookup by PID */
    process_t *proc = process_get_current();
    if (!proc || proc->task.pid != pid)
    {
        return -1; /* Process not found */
    }

    /* Set signal bit in pending mask */
    proc->signal_pending |= SIGNAL_MASK(signal);

#if DEBUG_PROCESS
    serial_print("[SIGNAL] Sent signal to process\n");
#endif

    return 0;
}

/**
 * handle_signals - Handle pending signals
 * Called by scheduler before switching to process
 */
void handle_signals(void)
{
    process_t *current = process_get_current();
    if (!current)
    {
        return;
    }

    /* Check for pending signals */
    if (current->signal_pending == 0)
    {
        return;
    }

    /* SIGKILL - immediate termination, cannot be caught */
    if (current->signal_pending & SIGNAL_MASK(SIGKILL))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGKILL received, terminating process\n");
#endif
        process_exit(-1);
        return; /* Never returns */
    }

    /* SIGTERM - graceful termination */
    if (current->signal_pending & SIGNAL_MASK(SIGTERM))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGTERM received, terminating process\n");
#endif
        process_exit(0);
        return; /* Never returns */
    }

    /* Clear handled signals */
    current->signal_pending = 0;
}
