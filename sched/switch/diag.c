/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: diag.c
 * Description: Switch-path diagnostics. The switch code calls
 *              switch_trace() once per event; the stable CTX CLASSIFY tags
 *              that harnesses match (scripts/ktm_log_classify.py) live here.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "switch_internal.h"
#include <ir0/klog.h>

void switch_trace(enum switch_trace_event event)
{
	switch (event)
	{
	case SWITCH_TRACE_KERNEL_RETURN_FRAME_REPAIR:
		klog_info("CTX", "CLASSIFY KERNEL_RETURN_FRAME_REPAIR");
		break;
	case SWITCH_TRACE_KERNEL_RETURN_UNREPAIRED_DEMOTE:
		klog_info("CTX", "CLASSIFY KERNEL_RETURN_UNREPAIRED_DEMOTE");
		break;
	}
}
