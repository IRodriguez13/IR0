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

/*
 * Pull in menuconfig-generated defines when available.
 * When building without menuconfig, the file won't exist and
 * the fallback defaults below keep everything enabled.
 */
#ifdef __has_include
#if __has_include(<generated/autoconf.h>)
#include <generated/autoconf.h>
#endif
#endif

/* MEMORY SUBSYSTEM DEBUG FLAGS                                              */

/* Memory Allocator Debug */
#if defined(CONFIG_DEBUG_MEMORY_ALLOCATOR) && CONFIG_DEBUG_MEMORY_ALLOCATOR
#define DEBUG_MEMORY_ALLOCATOR 1
#else
#define DEBUG_MEMORY_ALLOCATOR 0
#endif
#if defined(CONFIG_DEBUG_MEMORY_COALESCING) && CONFIG_DEBUG_MEMORY_COALESCING
#define DEBUG_MEMORY_COALESCING 1
#else
#define DEBUG_MEMORY_COALESCING 0
#endif
#if defined(CONFIG_DEBUG_MEMORY_STATS) && CONFIG_DEBUG_MEMORY_STATS
#define DEBUG_MEMORY_STATS 1
#else
#define DEBUG_MEMORY_STATS 0
#endif
#if defined(CONFIG_DEBUG_PMM) && CONFIG_DEBUG_PMM
#define DEBUG_PMM 1
#else
#define DEBUG_PMM 0
#endif


/* Paging Debug */
#if defined(CONFIG_DEBUG_PAGING) && CONFIG_DEBUG_PAGING
#define DEBUG_PAGING 1                /* Paging operations */
#else
#define DEBUG_PAGING 0
#endif
#if defined(CONFIG_DEBUG_PAGE_FAULTS) && CONFIG_DEBUG_PAGE_FAULTS
#define DEBUG_PAGE_FAULTS 1           /* Page fault handling */
#else
#define DEBUG_PAGE_FAULTS 0
#endif

/* PROCESS SUBSYSTEM DEBUG FLAGS                                             */

#if defined(CONFIG_DEBUG_PROCESS) && CONFIG_DEBUG_PROCESS
#define DEBUG_PROCESS 1               /* Process creation/destruction */
#else
#define DEBUG_PROCESS 0
#endif
#if defined(CONFIG_DEBUG_SCHEDULER) && CONFIG_DEBUG_SCHEDULER
#define DEBUG_SCHEDULER 1             /* Scheduler decisions */
#else
#define DEBUG_SCHEDULER 0
#endif
#if defined(CONFIG_DEBUG_FORK) && CONFIG_DEBUG_FORK
#define DEBUG_FORK 1                  /* Fork operations */
#else
#define DEBUG_FORK 0
#endif

/* FILESYSTEM DEBUG FLAGS                                                    */

#if defined(CONFIG_DEBUG_VFS) && CONFIG_DEBUG_VFS
#define DEBUG_VFS 1                   /* VFS operations */
#else
#define DEBUG_VFS 0
#endif
#if defined(CONFIG_DEBUG_FS_MOUNT) && CONFIG_DEBUG_FS_MOUNT
#define DEBUG_FS_MOUNT 1              /* Mount/unmount operations */
#else
#define DEBUG_FS_MOUNT 0
#endif

/* SYSCALL DEBUG FLAGS                                                       */

#if defined(CONFIG_DEBUG_SYSCALLS) && CONFIG_DEBUG_SYSCALLS
#define DEBUG_SYSCALLS 1              /* All syscall invocations */
#else
#define DEBUG_SYSCALLS 0
#endif
#if defined(CONFIG_DEBUG_SYSCALL_PARAMS) && CONFIG_DEBUG_SYSCALL_PARAMS
#define DEBUG_SYSCALL_PARAMS 1        /* Detailed syscall parameters */
#else
#define DEBUG_SYSCALL_PARAMS 0
#endif
#if defined(CONFIG_DEBUG_KEYBOARD) && CONFIG_DEBUG_KEYBOARD
#define DEBUG_KEYBOARD 1              /* Serial log each stdin read */
#else
#define DEBUG_KEYBOARD 0
#endif

/* KERNEL BEHAVIOR CONFIGURATION                                             */

/*
 * KERNEL_DEBUG_SHELL
 * 1: Test init — use integrated shell as PID 1 (init_1 / debshell). Not the real init.
 * 0: Real init — load and execute /sbin/init from the filesystem.
 *
 * CONFIG_KERNEL_DEBUG_SHELL from Makefile / autoconf overrides this value.
 */
#ifndef CONFIG_KERNEL_DEBUG_SHELL
#define KERNEL_DEBUG_SHELL 1
#else
#define KERNEL_DEBUG_SHELL CONFIG_KERNEL_DEBUG_SHELL
#endif

#ifndef CONFIG_TICK_RATE_HZ
#define CONFIG_TICK_RATE_HZ 1000
#endif

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

/* MEMORY LAYOUT — virtual addresses and segment selectors */
#define USER_STACK_TOP      0x7FFFF000UL
#define USER_STACK_SIZE     0x4000
#define USER_HEAP_BASE      0x2000000UL
#define USER_MMAP_START     0x8000000UL
#define USER_MMAP_END       0x7FFFF000UL
#define INIT_DEBUG_STACK_BASE 0x1000000UL

/*
 * Shared keyboard ring (kernel IRQ fills; userspace/debug may consume).
 * Layout: KEYBOARD_BUFFER_SIZE bytes data, then int write position.
 */
#define KEYBOARD_BUFFER_ADDR    0x500000UL
#define KEYBOARD_BUFFER_SIZE    4096

#define KERNEL_CODE_SEL     0x08
#define KERNEL_DATA_SEL     0x10
#define USER_CODE_SEL       0x1B
#define USER_DATA_SEL       0x23
#define RFLAGS_IF           0x202
#define PMM_PHYS_BASE       0x2000000
#define PMM_PHYS_SIZE       0x1000000

/* PROCESS LIMITS */
#define MAX_PROCESSES 256
#define MAX_FDS_PER_PROCESS 64
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
#define TIMER_FREQUENCY CONFIG_TICK_RATE_HZ

/* FEATURE FLAGS — CONFIG_ENABLE_* from Makefile / autoconf take precedence.
 * When building without menuconfig, everything defaults to enabled.              */

#define ENABLE_SMP 0          /* Symmetric multiprocessing (not implemented) */
#define ENABLE_USB 0          /* USB support (not implemented) */

#ifndef CONFIG_ENABLE_NETWORKING
#define CONFIG_ENABLE_NETWORKING 1
#endif

#ifndef CONFIG_ENABLE_SOUND
#define CONFIG_ENABLE_SOUND 1
#endif

#ifndef CONFIG_ENABLE_VBE
#define CONFIG_ENABLE_VBE 1
#endif

#ifndef CONFIG_ENABLE_MOUSE
#define CONFIG_ENABLE_MOUSE 1
#endif

#ifndef CONFIG_ENABLE_BLUETOOTH
#define CONFIG_ENABLE_BLUETOOTH 1
#endif

#ifndef CONFIG_ENABLE_EXAMPLE_DRIVERS
#define CONFIG_ENABLE_EXAMPLE_DRIVERS 0
#endif

/* Legacy aliases for code that still uses the old names */
#define ENABLE_NETWORKING CONFIG_ENABLE_NETWORKING
#define ENABLE_GRAPHICS   CONFIG_ENABLE_VBE
#define ENABLE_SOUND      CONFIG_ENABLE_SOUND

#endif /* _IR0_KERNEL_CONFIG_H */
