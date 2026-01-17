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

    /* Find target process by PID */
    process_t *proc = process_find_by_pid(pid);
    if (!proc)
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
 * 
 * Signals are handled in priority order:
 * 1. Unstoppable signals (SIGKILL, SIGSTOP)
 * 2. Error signals (SIGSEGV, SIGFPE, SIGILL, SIGBUS) - terminate process
 * 3. Termination signals (SIGTERM, SIGINT, SIGQUIT, SIGABRT)
 * 4. Other signals
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

    /* SIGKILL - immediate termination, cannot be caught or ignored */
    if (current->signal_pending & SIGNAL_MASK(SIGKILL))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGKILL received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGKILL);
        process_exit(-1);
        return; /* Never returns */
    }

    /* SIGSTOP - stop process (cannot be caught) */
    if (current->signal_pending & SIGNAL_MASK(SIGSTOP))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGSTOP received, stopping process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGSTOP);
        current->state = PROCESS_BLOCKED;
        return;
    }

    /* Error signals - terminate process immediately (prevent crashes) */
    if (current->signal_pending & SIGNAL_MASK(SIGSEGV))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGSEGV received (segmentation fault), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGSEGV);
        process_exit(139); /* Exit code 139 = 128 + 11 (SIGSEGV) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGFPE))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGFPE received (arithmetic error), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGFPE);
        process_exit(136); /* Exit code 136 = 128 + 8 (SIGFPE) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGILL))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGILL received (illegal instruction), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGILL);
        process_exit(132); /* Exit code 132 = 128 + 4 (SIGILL) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGBUS))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGBUS received (bus error), terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGBUS);
        process_exit(135); /* Exit code 135 = 128 + 7 (SIGBUS) */
        return;
    }

    /* Termination signals */
    if (current->signal_pending & SIGNAL_MASK(SIGTERM))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGTERM received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGTERM);
        process_exit(0);
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGINT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGINT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGINT);
        process_exit(130); /* Exit code 130 = 128 + 2 (SIGINT) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGQUIT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGQUIT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGQUIT);
        process_exit(131); /* Exit code 131 = 128 + 3 (SIGQUIT) */
        return;
    }

    if (current->signal_pending & SIGNAL_MASK(SIGABRT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGABRT received, terminating process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGABRT);
        process_exit(134); /* Exit code 134 = 128 + 6 (SIGABRT) */
        return;
    }

    /* SIGCONT - continue if stopped */
    if (current->signal_pending & SIGNAL_MASK(SIGCONT))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGCONT received, resuming process\n");
#endif
        current->signal_pending &= ~SIGNAL_MASK(SIGCONT);
        if (current->state == PROCESS_BLOCKED)
        {
            current->state = PROCESS_READY;
        }
    }

    /* SIGCHLD - child process terminated (handled by parent, clear here) */
    if (current->signal_pending & SIGNAL_MASK(SIGCHLD))
    {
#if DEBUG_PROCESS
        serial_print("[SIGNAL] SIGCHLD received (child terminated)\n");
#endif
        /* SIGCHLD is informational, parent should handle via wait() */
        current->signal_pending &= ~SIGNAL_MASK(SIGCHLD);
    }

    /* SIGALRM, SIGUSR1, SIGUSR2 - user signals (clear but don't handle yet) */
    current->signal_pending &= ~(SIGNAL_MASK(SIGALRM) | SIGNAL_MASK(SIGUSR1) | SIGNAL_MASK(SIGUSR2) | SIGNAL_MASK(SIGTRAP));
}
