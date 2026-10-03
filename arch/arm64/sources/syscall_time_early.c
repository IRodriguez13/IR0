/* SPDX-License-Identifier: GPL-3.0-only */
/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * Freestanding ARM64 time syscall provider for bring-up.
 */

#include "syscall_time_early.h"
#include "mmu_early.h"
#include "timer.h"

#include <ir0/boot_log.h>

#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38
#define CLOCK_MONOTONIC 1UL
#define NS_PER_SEC 1000000000ULL

struct linux_timespec64 {
	int64_t tv_sec;
	int64_t tv_nsec;
};

struct linux_timeval {
	int64_t tv_sec;
	int64_t tv_usec;
};

static int g_nanosleep_ok;
static int g_clock_gettime_ok;
static int g_gettimeofday_ok;
static int g_clock_nanosleep_ok;

static int copy_from_user(void *destination, uint64_t source, uint64_t size)
{
	uint8_t *dst = destination;
	const volatile uint8_t *src;
	uint64_t i;

	if (!arm64_mmu_user_buf_ok(source, size))
		return -EFAULT;
	src = (const volatile uint8_t *)(uintptr_t)source;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static int copy_to_user(uint64_t destination, const void *source, uint64_t size)
{
	volatile uint8_t *dst;
	const uint8_t *src = source;
	uint64_t i;

	if (!arm64_mmu_user_buf_ok(destination, size))
		return -EFAULT;
	dst = (volatile uint8_t *)(uintptr_t)destination;
	for (i = 0; i < size; i++)
		dst[i] = src[i];
	return 0;
}

static int64_t sleep_timespec_user(uint64_t request)
{
	struct linux_timespec64 value;
	uint64_t frequency;
	uint64_t delta;
	uint64_t deadline;

	if (copy_from_user(&value, request, sizeof(value)) != 0)
		return -EFAULT;
	if (value.tv_sec < 0 || value.tv_nsec < 0 ||
	    value.tv_nsec >= (int64_t)NS_PER_SEC)
		return -EINVAL;
	frequency = timer_get_frequency();
	if (frequency == 0)
		return -EINVAL;
	if ((uint64_t)value.tv_sec > UINT64_MAX / frequency)
		return -EINVAL;
	delta = (uint64_t)value.tv_sec * frequency;
	delta += ((uint64_t)value.tv_nsec * frequency) / NS_PER_SEC;
	if (delta == 0)
		delta = 1;
	deadline = timer_read() + delta;
	while (timer_read() < deadline)
		__asm__ volatile("yield" ::: "memory");
	return 0;
}

static int64_t early_clock_gettime(uint64_t clock_id, uint64_t output)
{
	struct linux_timespec64 value;
	uint64_t frequency;
	uint64_t counter;

	if (clock_id != CLOCK_MONOTONIC)
		return -EINVAL;
	frequency = timer_get_frequency();
	if (frequency == 0)
		return -EINVAL;
	counter = timer_read();
	value.tv_sec = (int64_t)(counter / frequency);
	value.tv_nsec = (int64_t)(((counter % frequency) * NS_PER_SEC) / frequency);
	if (copy_to_user(output, &value, sizeof(value)) != 0)
		return -EFAULT;
	g_clock_gettime_ok = 1;
	ir0_boot_smoke("ARM64_CLOCK_GETTIME_OK");
	return 0;
}

static int64_t early_gettimeofday(uint64_t output)
{
	struct linux_timeval value;
	uint64_t frequency = timer_get_frequency();
	uint64_t counter;

	if (frequency == 0)
		return -EINVAL;
	counter = timer_read();
	value.tv_sec = (int64_t)(counter / frequency);
	value.tv_usec = (int64_t)(((counter % frequency) * 1000000ULL) / frequency);
	if (copy_to_user(output, &value, sizeof(value)) != 0)
		return -EFAULT;
	g_gettimeofday_ok = 1;
	ir0_boot_smoke("ARM64_GETTIMEOFDAY_OK");
	return 0;
}

static int64_t early_time_syscall(void *context, enum ir0_syscall_id id,
				  uint64_t a0, uint64_t a1, uint64_t a2,
				  uint64_t a3, uint64_t a4, uint64_t a5)
{
	int64_t result;

	(void)context;
	(void)a4;
	(void)a5;
	switch (id) {
	case IR0_SYSCALL_NANOSLEEP:
		result = sleep_timespec_user(a0);
		if (result == 0) {
			g_nanosleep_ok = 1;
			ir0_boot_smoke("ARM64_NANOSLEEP_OK");
		}
		return result;
	case IR0_SYSCALL_CLOCK_GETTIME:
		return early_clock_gettime(a0, a1);
	case IR0_SYSCALL_CLOCK_NANOSLEEP:
		if (a0 != CLOCK_MONOTONIC)
			return -EINVAL;
		(void)a1;
		(void)a3;
		result = sleep_timespec_user(a2);
		if (result == 0) {
			g_clock_nanosleep_ok = 1;
			ir0_boot_smoke("ARM64_CLOCK_NANOSLEEP_OK");
		}
		return result;
	case IR0_SYSCALL_GETTIMEOFDAY:
		(void)a1;
		return early_gettimeofday(a0);
	case IR0_SYSCALL_CLOCK_GETRES:
		return 0;
	default:
		return -ENOSYS;
	}
}

int arm64_early_time_smoke_ok(void)
{
	return g_nanosleep_ok && g_clock_gettime_ok && g_gettimeofday_ok &&
	       g_clock_nanosleep_ok;
}

static const enum ir0_syscall_id g_time_syscalls[] = {
	IR0_SYSCALL_NANOSLEEP,
	IR0_SYSCALL_CLOCK_GETTIME,
	IR0_SYSCALL_CLOCK_NANOSLEEP,
	IR0_SYSCALL_GETTIMEOFDAY,
	IR0_SYSCALL_CLOCK_GETRES,
};

const struct syscall_context_provider arm64_early_time_provider = {
	.ids = g_time_syscalls,
	.count = sizeof(g_time_syscalls) / sizeof(g_time_syscalls[0]),
	.handler = early_time_syscall,
};
