/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: logging.h
 * Description: Subsystem log API — thin aliases of the klog hub.
 *
 * Like Linux pr_info() over printk(), every log_* call is a klog_* call:
 * one va_list path, one level filter (ir0.loglevel / klog_set_level) and one
 * record ring (dmesg, /dev/kmsg, /proc/kmsg). Nothing here keeps state.
 */

#pragma once

#include <ir0/ktm/klog.h>

#define log_debug(component, message) klog_debug(component, message)
#define log_info(component, message) klog_info(component, message)
#define log_warn(component, message) klog_warn(component, message)
#define log_error(component, message) klog_error(component, message)
#define log_fatal(component, message) klog_fatal(component, message)

#define log_debug_fmt(component, ...) klog_debug_fmt(component, __VA_ARGS__)
#define log_info_fmt(component, ...) klog_info_fmt(component, __VA_ARGS__)
#define log_warn_fmt(component, ...) klog_warn_fmt(component, __VA_ARGS__)
#define log_error_fmt(component, ...) klog_error_fmt(component, __VA_ARGS__)
#define log_fatal_fmt(component, ...) klog_fatal_fmt(component, __VA_ARGS__)

#define log_subsystem_ok(subsystem_name) \
	klog_info(subsystem_name, "Registered and Initialized OK")

#define LOG_DEBUG(component, message) log_debug(component, message)
#define LOG_INFO(component, message) log_info(component, message)
#define LOG_WARNING(component, message) log_warn(component, message)
#define LOG_ERROR(component, message) log_error(component, message)
#define LOG_FATAL(component, message) log_fatal(component, message)

#define LOG_DEBUG_FMT(component, ...) log_debug_fmt(component, __VA_ARGS__)
#define LOG_INFO_FMT(component, ...) log_info_fmt(component, __VA_ARGS__)
#define LOG_WARNING_FMT(component, ...) log_warn_fmt(component, __VA_ARGS__)
#define LOG_ERROR_FMT(component, ...) log_error_fmt(component, __VA_ARGS__)
#define LOG_FATAL_FMT(component, ...) log_fatal_fmt(component, __VA_ARGS__)
