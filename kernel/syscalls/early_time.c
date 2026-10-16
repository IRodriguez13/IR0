/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral POSIX time semantics for staged userspace. */

#include <stdint.h>
#include <ir0/early_syscall.h>
#include <ir0/early_time.h>
#include <ir0/time.h>

#define EFAULT 14
#define EINVAL 22
#define ENOSYS 38
#define CLOCK_MONOTONIC 1UL
#define NS_PER_SEC 1000000000ULL

static int g_nanosleep_ok;
static int g_clock_gettime_ok;
static int g_gettimeofday_ok;
static int g_clock_nanosleep_ok;

static const struct early_syscall_services *services(void *opaque)
{
	struct early_syscall_context *context = opaque;

	return context ? context->services : 0;
}

static int64_t sleep_timespec_user(void *opaque, uint64_t request)
{
	const struct early_syscall_services *ops = services(opaque);
	struct timespec value;
	uint64_t frequency, seconds_ticks, nanosecond_ticks, delta, start;

	if (!ops || !ops->copy_from_user || !ops->counter_read ||
	    !ops->counter_frequency || !ops->cpu_relax)
		return -ENOSYS;
	if (ops->copy_from_user(&value, request, sizeof(value)) != 0)
		return -EFAULT;
	if (value.tv_sec < 0 || value.tv_nsec < 0 ||
	    value.tv_nsec >= (int64_t)NS_PER_SEC)
		return -EINVAL;
	frequency = ops->counter_frequency();
	if (!frequency || (uint64_t)value.tv_sec > UINT64_MAX / frequency)
		return -EINVAL;
	seconds_ticks = (uint64_t)value.tv_sec * frequency;
	nanosecond_ticks = ((uint64_t)value.tv_nsec * frequency) / NS_PER_SEC;
	if (nanosecond_ticks > UINT64_MAX - seconds_ticks)
		return -EINVAL;
	delta = seconds_ticks + nanosecond_ticks;
	if (delta > UINT64_MAX / 2)
		return -EINVAL;
	if (!delta)
		delta = 1;
	start = ops->counter_read();
	while (ops->counter_read() - start < delta)
		ops->cpu_relax();
	return 0;
}

static int64_t early_clock_gettime(void *opaque, uint64_t clock_id,
				   uint64_t output)
{
	const struct early_syscall_services *ops = services(opaque);
	struct timespec value;
	uint64_t frequency, counter;

	if (clock_id != CLOCK_MONOTONIC)
		return -EINVAL;
	if (!ops || !ops->copy_to_user || !ops->counter_read ||
	    !ops->counter_frequency)
		return -ENOSYS;
	frequency = ops->counter_frequency();
	if (!frequency)
		return -EINVAL;
	counter = ops->counter_read();
	value.tv_sec = (int64_t)(counter / frequency);
	value.tv_nsec = (int64_t)(((counter % frequency) * NS_PER_SEC) / frequency);
	if (ops->copy_to_user(output, &value, sizeof(value)) != 0)
		return -EFAULT;
	g_clock_gettime_ok = 1;
	if (ops->note)
		ops->note(EARLY_SYSCALL_CLOCK_GETTIME_OK);
	return 0;
}

static int64_t early_gettimeofday(void *opaque, uint64_t output)
{
	const struct early_syscall_services *ops = services(opaque);
	struct timeval value;
	uint64_t frequency, counter;

	if (!ops || !ops->copy_to_user || !ops->counter_read ||
	    !ops->counter_frequency)
		return -ENOSYS;
	frequency = ops->counter_frequency();
	if (!frequency)
		return -EINVAL;
	counter = ops->counter_read();
	value.tv_sec = (int64_t)(counter / frequency);
	value.tv_usec = (int64_t)(((counter % frequency) * 1000000ULL) / frequency);
	if (ops->copy_to_user(output, &value, sizeof(value)) != 0)
		return -EFAULT;
	g_gettimeofday_ok = 1;
	if (ops->note)
		ops->note(EARLY_SYSCALL_GETTIMEOFDAY_OK);
	return 0;
}

static int64_t early_time_syscall(void *opaque, enum ir0_syscall_id id,
				  uint64_t a0, uint64_t a1, uint64_t a2,
				  uint64_t a3, uint64_t a4, uint64_t a5)
{
	const struct early_syscall_services *ops = services(opaque);
	int64_t result;

	(void)a4; (void)a5;
	switch (id) {
	case IR0_SYSCALL_NANOSLEEP:
		result = sleep_timespec_user(opaque, a0);
		if (!result) {
			g_nanosleep_ok = 1;
			if (ops && ops->note) ops->note(EARLY_SYSCALL_NANOSLEEP_OK);
		}
		return result;
	case IR0_SYSCALL_CLOCK_GETTIME:
		return early_clock_gettime(opaque, a0, a1);
	case IR0_SYSCALL_CLOCK_NANOSLEEP:
		if (a0 != CLOCK_MONOTONIC) return -EINVAL;
		(void)a1; (void)a3;
		result = sleep_timespec_user(opaque, a2);
		if (!result) {
			g_clock_nanosleep_ok = 1;
			if (ops && ops->note) ops->note(EARLY_SYSCALL_CLOCK_NANOSLEEP_OK);
		}
		return result;
	case IR0_SYSCALL_GETTIMEOFDAY:
		(void)a1;
		return early_gettimeofday(opaque, a0);
	case IR0_SYSCALL_CLOCK_GETRES:
		return 0;
	default:
		return -ENOSYS;
	}
}

int early_time_smoke_ok(void)
{
	return g_nanosleep_ok && g_clock_gettime_ok && g_gettimeofday_ok &&
	       g_clock_nanosleep_ok;
}

static const enum ir0_syscall_id g_time_syscalls[] = {
	IR0_SYSCALL_NANOSLEEP, IR0_SYSCALL_CLOCK_GETTIME,
	IR0_SYSCALL_CLOCK_NANOSLEEP, IR0_SYSCALL_GETTIMEOFDAY,
	IR0_SYSCALL_CLOCK_GETRES,
};

const struct syscall_context_provider early_time_provider = {
	.ids = g_time_syscalls,
	.count = sizeof(g_time_syscalls) / sizeof(g_time_syscalls[0]),
	.handler = early_time_syscall,
};
