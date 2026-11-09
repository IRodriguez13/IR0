/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: switch_internal.h
 * Description: Private to sched/switch: kernel-resume preparation and the
 *              switch diag/trace hook.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/task.h>

/* Settle @next's blocked-syscall state before its kernel continuation runs. */
void context_prepare_kernel_resume(task_t *next);

/* Events the switch path reports; tag text lives in diag.c. */
enum switch_trace_event
{
	SWITCH_TRACE_KERNEL_RETURN_WAIT_DEMOTE,
	SWITCH_TRACE_KERNEL_RETURN_FRAME_REPAIR,
	SWITCH_TRACE_KERNEL_RETURN_UNREPAIRED_DEMOTE,
};

void switch_trace(enum switch_trace_event event);
