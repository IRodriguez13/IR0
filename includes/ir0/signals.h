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


/* Standard Unix signals - essential set for error handling */
/* Hardware/CPU exceptions */
#define SIGSEGV  11   /* Segmentation violation (invalid memory access) */
#define SIGFPE    8   /* Floating point exception (divide by zero, overflow) */
#define SIGILL    4   /* Illegal instruction */
#define SIGBUS    7   /* Bus error (misaligned memory access) */
#define SIGTRAP   5   /* Trace/breakpoint trap */

/* Termination signals */
#define SIGKILL   9   /* Kill process (cannot be caught or ignored) */
#define SIGTERM  15   /* Termination signal (can be caught) */
#define SIGINT    2   /* Interrupt from keyboard (Ctrl+C) */
#define SIGQUIT   3   /* Quit from keyboard (Ctrl+\\) */

/* Process control */
#define SIGCHLD  17   /* Child process terminated or stopped */
#define SIGSTOP  19   /* Stop process (cannot be caught) */
#define SIGCONT  18   /* Continue if stopped */

/* Other */
#define SIGABRT   6   /* Abort signal (from abort()) */
#define SIGALRM  14   /* Timer signal (from alarm()) */
#define SIGUSR1  10   /* User-defined signal 1 */
#define SIGUSR2  12   /* User-defined signal 2 */

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
