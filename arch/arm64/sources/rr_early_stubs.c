/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: rr_early_stubs.c
 * Description: Weak stubs so sched/rr_sched.c links into freestanding ARM64.
 */

#include <stddef.h>
#include <stdint.h>

#include <ir0/process.h>
#include <ir0/boot_log.h>

static uint8_t g_heap[8192] __attribute__((aligned(16)));
static size_t g_heap_off;

void *__attribute__((weak)) __kmalloc_checked(size_t n, const char *file, int line,
					      const char *caller)
{
	size_t align = 16;
	size_t off;

	(void)file;
	(void)line;
	(void)caller;
	off = (g_heap_off + align - 1U) & ~(align - 1U);
	if (off > sizeof(g_heap) || n > sizeof(g_heap) - off)
		return NULL;
	g_heap_off = off + n;
	return &g_heap[off];
}

void __attribute__((weak)) __kfree_checked(void *p, const char *file, int line,
					   const char *caller)
{
	(void)p;
	(void)file;
	(void)line;
	(void)caller;
}

void __attribute__((weak)) handle_signals(void)
{
}

int __attribute__((weak)) signals_should_handle_on_run(process_t *p)
{
	(void)p;
	return 0;
}

void __attribute__((weak)) clock_note_context_switch(void)
{
}

void __attribute__((weak)) process_sched_state_trace(const process_t *p,
					      process_state_t prev,
					      process_state_t next,
					      void *caller)
{
	(void)p;
	(void)prev;
	(void)next;
	(void)caller;
}

/* Portable blockdev.c CLASSIFY lines; full ktm/klog.c is not in early boot. */
void __attribute__((weak)) klog_info(const char *component, const char *message)
{
	ir0_boot_info(component, message);
}
