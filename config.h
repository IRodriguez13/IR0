// SPDX-License-Identifier: GPL-3.0-only
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * File: config.h
 * Description: Unified kernel configuration and debug flags
 *              This is the central configuration file for the entire project
 */

#ifndef _IR0_KERNEL_CONFIG_H
#define _IR0_KERNEL_CONFIG_H

/* MEMORY SUBSYSTEM DEBUG FLAGS                                              */

/* Memory Allocator Debug */
#define DEBUG_MEMORY_ALLOCATOR 1      /* Enable allocator debug output */
#define DEBUG_MEMORY_COALESCING 1     /* Track coalescing operations */
#define DEBUG_MEMORY_STATS 1          /* Show allocation statistics */
#define DEBUG_PMM 1                   /* Physical memory manager debug */


/* Paging Debug */
#define DEBUG_PAGING 0                /* Paging operations */
#define DEBUG_PAGE_FAULTS 0           /* Page fault handling */

/* PROCESS SUBSYSTEM DEBUG FLAGS                                             */

#define DEBUG_PROCESS 0               /* Process creation/destruction */
#define DEBUG_SCHEDULER 0             /* Scheduler decisions */
#define DEBUG_FORK 0                  /* Fork operations */

/* FILESYSTEM DEBUG FLAGS                                                    */

#define DEBUG_VFS 0                   /* VFS operations */
#define DEBUG_FS_MOUNT 0              /* Mount/unmount operations */

/* SYSCALL DEBUG FLAGS                                                       */

#define DEBUG_SYSCALLS 0              /* All syscall invocations */
#define DEBUG_SYSCALL_PARAMS 0        /* Detailed syscall parameters */

/* KERNEL BEHAVIOR CONFIGURATION                                             */

/* 
 * KERNEL_DEBUG_SHELL
 * 1: Use the built-in kernel shell (init_1)
 * 0: Load and execute /bin/init from the filesystem
 */
#define KERNEL_DEBUG_SHELL 1

/* COMPATIBILITY - Keep existing definitions                                 */

/* These are included for backward compatibility with includes/ir0/config.h */
#ifdef DEBUG
#define KERNEL_DEBUG 1
#define VERBOSE_LOGGING 1
#else
#define KERNEL_DEBUG 0
#define VERBOSE_LOGGING 0
#endif

#endif /* _IR0_KERNEL_CONFIG_H */
