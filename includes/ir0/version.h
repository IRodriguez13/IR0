/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Version Information
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: version.h
 * Description: Centralized kernel version configuration
 *              This is the single source of truth for kernel version.
 */

#ifndef _IR0_VERSION_H
#define _IR0_VERSION_H

/* KERNEL VERSION INFORMATION */
/* 
 * Format: MAJOR.MINOR.PATCH[-SUFFIX]
 * - MAJOR: Major version number (incompatible API changes)
 * - MINOR: Minor version number (backward-compatible functionality)
 * - PATCH: Patch version number (backward-compatible bug fixes)
 * - SUFFIX: Optional pre-release identifier (e.g., "pre-rc1", "beta1")
 * 
 * Examples:
 *   - "1.0.0" - Stable release
 *   - "0.1.0-pre-rc1" - Pre-release candidate
 *   - "1.2.3-beta2" - Beta release
 * 
 * Usage:
 *   - /proc/version: Shows "IR0 version X.Y.Z (built DATE TIME)"
 *   - uname -a: Shows kernel version string
 *   - sys_kernel_info: Returns version string to userspace
 */
#define IR0_VERSION_MAJOR 0
#define IR0_VERSION_MINOR 0
#define IR0_VERSION_PATCH 1
#define IR0_VERSION_SUFFIX "-pre-rc3"
#define IR0_VERSION_STRING "0.0.1-pre-rc3"

/* Build information macros */
/* If passed from Makefile, use those (real data), otherwise use compiler defaults */
#ifndef IR0_BUILD_DATE_STRING
#define IR0_BUILD_DATE_STRING __DATE__
#endif

#ifndef IR0_BUILD_TIME_STRING
#define IR0_BUILD_TIME_STRING __TIME__
#endif

#ifndef IR0_BUILD_USER_STRING
#define IR0_BUILD_USER_STRING "unknown"
#endif

#ifndef IR0_BUILD_HOST_STRING
#define IR0_BUILD_HOST_STRING "localhost"
#endif

#ifndef IR0_BUILD_CC_STRING
#define IR0_BUILD_CC_STRING "gcc unknown"
#endif

#ifndef IR0_BUILD_NUMBER_STRING
#define IR0_BUILD_NUMBER_STRING "1"
#endif

/* Convenience macros (for compatibility and easier use) */
#define IR0_BUILD_DATE IR0_BUILD_DATE_STRING
#define IR0_BUILD_TIME IR0_BUILD_TIME_STRING
#define IR0_BUILD_USER IR0_BUILD_USER_STRING
#define IR0_BUILD_HOST IR0_BUILD_HOST_STRING
#define IR0_BUILD_CC IR0_BUILD_CC_STRING
#define IR0_BUILD_NUMBER IR0_BUILD_NUMBER_STRING

#define IR0_BUILD_INFO IR0_VERSION_STRING " (built " IR0_BUILD_DATE " " IR0_BUILD_TIME " by " IR0_BUILD_USER "@" IR0_BUILD_HOST " with " IR0_BUILD_CC ")"

#endif /* _IR0_VERSION_H */

