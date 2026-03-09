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
#define DEBUG_MEMORY_ALLOCATOR 0      /* Enable allocator debug output */
#define DEBUG_MEMORY_COALESCING 0     /* Track coalescing operations */
#define DEBUG_MEMORY_STATS 0          /* Show allocation statistics */
#define DEBUG_PMM 0                   /* Physical memory manager debug */


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
 * 1: Test init — use integrated shell as PID 1 (init_1 / debshell). Not the real init.
 * 0: Real init — load and execute /sbin/init from the filesystem.
 */
#define KERNEL_DEBUG_SHELL 1

/*
 * IR0_KERNEL_TESTS
 * Include in-kernel test suite and cmd_ktest in debug shell.
 * Default: undefined (make ir0) - tests not linked.
 * Makefile sets -DIR0_KERNEL_TESTS=1 for kernel-x64-test.bin / make tests.
 * See Makefile target kernel-x64-test.bin.
 */

/*
 * KERNEL_ENABLE_EXAMPLE_DRIVERS
 * Enable multi-language example drivers registration in kernel
 * 
 * This flag is automatically set by the Makefile when drivers are enabled.
 * Default: 0 (disabled)
 * 
 * To enable: make en-ext-drv && make ir0
 * The Makefile will automatically pass -DKERNEL_ENABLE_EXAMPLE_DRIVERS=1
 * 
 * Note: This flag controls both compilation and runtime registration
 */
#ifndef KERNEL_ENABLE_EXAMPLE_DRIVERS
#define KERNEL_ENABLE_EXAMPLE_DRIVERS 0
#endif

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
/* Version is now centralized in includes/ir0/version.h */
#include <ir0/version.h>

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


/* FILESYSTEM CONFIGURATION */
#define MAX_PATH_LENGTH 256
#define MAX_FILENAME_LENGTH 64
#define VFS_MAX_OPEN_FILES 128

/* HARDWARE LIMITS */
#define MAX_CPUS 8
#define MAX_IRQ_HANDLERS 256
#define TIMER_FREQUENCY 1000  // 1000 Hz timer

/* FEATURE FLAGS */
#define ENABLE_SMP 0          /* Symmetric multiprocessing (not implemented) */
#define ENABLE_NETWORKING 1   /* Network stack (IP, ICMP, UDP, ARP, DNS, RTL8139, e1000) */
#define ENABLE_GRAPHICS 1     /* Graphics subsystem (VGA, VBE) */
#define ENABLE_SOUND 1        /* Sound Blaster support */
#define ENABLE_USB 0          /* USB support (not implemented) */

#endif /* _IR0_KERNEL_CONFIG_H */
