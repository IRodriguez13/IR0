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

/* KERNEL VERSION AND BUILD INFO */
#define IR0_VERSION_MAJOR 0
#define IR0_VERSION_MINOR 0
#define IR0_VERSION_PATCH 1
#define IR0_VERSION_STRING "0.0.1-pre-rc1"

/* MEMORY CONFIGURATION */
#define DEFAULT_STACK_SIZE (4 * 1024)    // 4KB stack per process
#define KERNEL_HEAP_SIZE (16 * 1024 * 1024) // 16MB kernel heap
#define USER_HEAP_MAX_SIZE (256 * 1024 * 1024) // 256MB max per process

/* PROCESS LIMITS */
#define MAX_PROCESSES 256
#define MAX_FDS_PER_PROCESS 32
#define MAX_NICE 19
#define MIN_NICE -20
#define DEFAULT_NICE 0

/* SCHEDULER CONFIGURATION */
#define CFS_TARGETED_LATENCY 20000000ULL  // 20ms in nanoseconds
#define CFS_MIN_GRANULARITY 4000000ULL    // 4ms minimum per process
#define CFS_NICE_0_LOAD 1024              // Weight of nice 0
#define MAX_RB_NODES 1024                 // RB-tree node pool size

/* FILESYSTEM CONFIGURATION */
#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 64
#define VFS_MAX_OPEN_FILES 128

/* HARDWARE LIMITS */
#define MAX_CPUS 8
#define MAX_IRQ_HANDLERS 256
#define TIMER_FREQUENCY 1000  // 1000 Hz timer

/* FEATURE FLAGS */
#define ENABLE_SMP 0          // Symmetric multiprocessing (not implemented)
#define ENABLE_NETWORKING 0   // Network stack (not implemented)
#define ENABLE_GRAPHICS 0     // Graphics subsystem (basic VGA only)
#define ENABLE_SOUND 1        // Sound Blaster support
#define ENABLE_USB 0          // USB support (not implemented)

#endif /* _IR0_KERNEL_CONFIG_H */
