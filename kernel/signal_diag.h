/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: signal_diag.h
 * Description: Signal delivery trace hook (private to kernel/signals.c).
 */

#pragma once

#include <stdint.h>

typedef struct process process_t;

/* What signal_trace() reports (Linux trace/events/signal.h style). */
enum signal_trace_event
{
	SIGNAL_TRACE_SEND,         /* @sig queued on @p */
	SIGNAL_TRACE_WAKE,         /* a blocked @p woken by @sig */
	SIGNAL_TRACE_DELIVER_FAULT, /* @sig handler entered from a fault: a=addr b=ip c=handler */
	SIGNAL_TRACE_ABANDON,      /* handler frame dropped after longjmp: a=user sp b=frame sp */
	SIGNAL_TRACE_KILL_SEGV,    /* pending SIGSEGV without a handler ends @p */
};

/*
 * signal_trace - Report one signal event. The policy code calls this and
 * nothing else; tags, build switches and KTM records live in signal_diag.c.
 */
void signal_trace(enum signal_trace_event ev, const process_t *p, int sig,
		  uint64_t a, uint64_t b, uint64_t c);
