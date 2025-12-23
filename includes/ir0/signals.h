// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: signals.h
 * Description: Basic signal definitions for process management
 */

#ifndef _IR0_SIGNALS_H
#define _IR0_SIGNALS_H

#include <stdint.h>


/* Standard Unix signals - minimal set */
#define SIGKILL   9   /* Kill process (cannot be caught or ignored) */
#define SIGTERM  15   /* Termination signal (can be caught) */
#define SIGCHLD  17   /* Child process terminated or stopped */

/* Signal bitmask helpers */
#define SIGNAL_MASK(sig) (1U << (sig))

/* Maximum signal number */
#define _NSIG 32


/**
 * send_signal - Send a signal to a process
 * @pid: Target process ID
 * @signal: Signal number to send
 *
 * Returns: 0 on success, -1 on error
 */
int send_signal(int pid, int signal);

/**
 * handle_signals - Check and handle pending signals for current process
 *
 * Called by scheduler before context switch
 */
void handle_signals(void);

#endif /* _IR0_SIGNALS_H */
