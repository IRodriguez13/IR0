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
#include <sched/task.h>

static uint8_t g_heap[8192] __attribute__((aligned(16)));
static unsigned g_heap_off;

void __attribute__((weak)) panic(const char *msg)
{
	(void)msg;
	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

void __attribute__((weak)) panicex(const char *msg)
{
	panic(msg ? msg : "panicex");
}

void *__attribute__((weak)) __kmalloc_checked(size_t n, const char *file, int line,
					      const char *caller)
{
	unsigned align = 16;
	unsigned off;

	(void)file;
	(void)line;
	(void)caller;
	off = (g_heap_off + align - 1U) & ~(align - 1U);
	if (off + n > sizeof(g_heap))
		return NULL;
	g_heap_off = off + (unsigned)n;
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

void __attribute__((weak)) arch_set_current_kernel_stack(process_t *p)
{
	(void)p;
}

void __attribute__((weak)) first_switch_to(struct process *next)
{
	extern void switch_context_arm64(task_t *prev, task_t *next);

	/*
	 * sched_context_switch_to() sets current_process = next before this call,
	 * so do not use current_process as prev — jump into @next only.
	 */
	if (!next)
	{
		for (;;)
			__asm__ volatile("wfi" ::: "memory");
	}

	switch_context_arm64(NULL, &next->task);

	for (;;)
		__asm__ volatile("wfi" ::: "memory");
}

/* Portable blockdev.c (includes/ir0/blockdev.c) needs strncmp freestanding. */
int __attribute__((weak)) strncmp(const char *a, const char *b, size_t n)
{
	while (n && *a && *a == *b)
	{
		a++;
		b++;
		n--;
	}
	if (!n)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

void __attribute__((weak)) switch_to(task_t *prev, task_t *next)
{
	extern void switch_context_arm64(task_t *a, task_t *b);

	switch_context_arm64(prev, next);
}
