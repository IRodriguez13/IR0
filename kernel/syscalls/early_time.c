/* SPDX-License-Identifier: GPL-3.0-only */
/** Architecture-neutral POSIX time semantics for staged userspace. */

#include <stdint.h>
#include <ir0/early_syscall.h>
#include <ir0/early_time.h>
#include <ir0/time.h>

#include <ir0/errno.h>
#define CLOCK_REALTIME 0UL
#define CLOCK_MONOTONIC 1UL
#define CLOCK_MONOTONIC_RAW 4UL
#define CLOCK_REALTIME_COARSE 5UL
#define CLOCK_MONOTONIC_COARSE 6UL
#define CLOCK_BOOTTIME 7UL
#define NS_PER_SEC 1000000000ULL

static int clock_supported(uint64_t clock_id)
{
	return clock_id == CLOCK_REALTIME ||
	       clock_id == CLOCK_MONOTONIC ||
	       clock_id == CLOCK_MONOTONIC_RAW ||
	       clock_id == CLOCK_REALTIME_COARSE ||
	       clock_id == CLOCK_MONOTONIC_COARSE ||
	       clock_id == CLOCK_BOOTTIME;
}

static int g_nanosleep_ok;
static int g_clock_gettime_ok;
static int g_gettimeofday_ok;
static int g_clock_nanosleep_ok;
static struct itimerval g_itimer[3];
static int64_t g_realtime_off;
static int64_t g_realtime_nsec;

static const struct early_syscall_services *services(void *opaque)
{
	struct early_syscall_context *context = opaque;

	return context ? context->services : 0;
}

static int64_t sleep_timespec_user(void *opaque, uint64_t request)
{
	const struct early_syscall_services *services_all = services(opaque);
	const struct early_user_services *user = services_all ? services_all->user : 0;
	const struct early_time_services *ops = services_all ? services_all->time : 0;
	struct timespec value;
	uint64_t frequency, seconds_ticks, nanosecond_ticks, delta, start;

	if (!user || !user->copy_from_user || !ops || !ops->counter_read ||
	    !ops->counter_frequency || !ops->cpu_relax)
		return -ENOSYS;
	if (user->copy_from_user(&value, request, sizeof(value)) != 0)
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
	const struct early_syscall_services *services_all = services(opaque);
	const struct early_user_services *user = services_all ? services_all->user : 0;
	const struct early_time_services *ops = services_all ? services_all->time : 0;
	struct timespec value;
	uint64_t frequency, counter;

	if (!clock_supported(clock_id))
		return -EINVAL;
	if (!user || !user->copy_to_user || !ops || !ops->counter_read ||
	    !ops->counter_frequency)
		return -ENOSYS;
	frequency = ops->counter_frequency();
	if (!frequency)
		return -EINVAL;
	counter = ops->counter_read();
	value.tv_sec = (int64_t)(counter / frequency);
	value.tv_nsec = (int64_t)(((counter % frequency) * NS_PER_SEC) / frequency);
	if (clock_id == CLOCK_REALTIME || clock_id == CLOCK_REALTIME_COARSE)
	{
		value.tv_sec += g_realtime_off;
		value.tv_nsec += g_realtime_nsec;
		if (value.tv_nsec >= (int64_t)NS_PER_SEC)
		{
			value.tv_sec++;
			value.tv_nsec -= (int64_t)NS_PER_SEC;
		}
	}
	if (user->copy_to_user(output, &value, sizeof(value)) != 0)
		return -EFAULT;
	g_clock_gettime_ok = 1;
	if (ops->note)
		ops->note(EARLY_SYSCALL_CLOCK_GETTIME_OK);
	return 0;
}

static int64_t early_gettimeofday(void *opaque, uint64_t output)
{
	const struct early_syscall_services *services_all = services(opaque);
	const struct early_user_services *user = services_all ? services_all->user : 0;
	const struct early_time_services *ops = services_all ? services_all->time : 0;
	struct timeval value;
	uint64_t frequency, counter;

	if (!user || !user->copy_to_user || !ops || !ops->counter_read ||
	    !ops->counter_frequency)
		return -ENOSYS;
	frequency = ops->counter_frequency();
	if (!frequency)
		return -EINVAL;
	counter = ops->counter_read();
	value.tv_sec = (int64_t)(counter / frequency) + g_realtime_off;
	value.tv_usec = (int64_t)(((counter % frequency) * 1000000ULL) / frequency) +
			g_realtime_nsec / 1000;
	if (value.tv_usec >= 1000000)
	{
		value.tv_sec++;
		value.tv_usec -= 1000000;
	}
	if (user->copy_to_user(output, &value, sizeof(value)) != 0)
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
	const struct early_syscall_services *services_all = services(opaque);
	const struct early_time_services *ops = services_all ? services_all->time : 0;
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
	case IR0_SYSCALL_CLOCK_SETTIME:
	{
		struct timespec req;
		const struct early_user_services *user;

		if (!clock_supported(a0))
			return -EINVAL;
		if (a0 != CLOCK_REALTIME)
			return -EPERM;
		if (!services_all || !(user = services_all->user) ||
		    !user->copy_from_user)
			return -ENOSYS;
		if (user->copy_from_user(&req, a1, sizeof(req)) != 0)
			return -EFAULT;
		return early_time_set_realtime(opaque, req.tv_sec, req.tv_nsec);
	}
	case IR0_SYSCALL_SETTIMEOFDAY:
	{
		struct timeval tv;
		const struct early_user_services *user;

		if (!a0)
			return 0;
		if (!services_all || !(user = services_all->user) ||
		    !user->copy_from_user)
			return -ENOSYS;
		if (user->copy_from_user(&tv, a0, sizeof(tv)) != 0)
			return -EFAULT;
		if (tv.tv_usec < 0 || tv.tv_usec >= 1000000)
			return -EINVAL;
		return early_time_set_realtime(opaque, tv.tv_sec,
					       tv.tv_usec * 1000);
	}
	case IR0_SYSCALL_ADJTIMEX:
		if (!a0)
			return -EFAULT;
		a1 = a0;
		a0 = CLOCK_REALTIME;
		__attribute__((fallthrough));
	case IR0_SYSCALL_CLOCK_ADJTIME:
	{
		uint8_t tx[IR0_EARLY_TIMEX_BYTES];
		const struct early_user_services *user;
		uint32_t modes;
		uint64_t frequency;
		int64_t now_sec;
		int64_t now_nsec;
		unsigned i;

		if (a0 != CLOCK_REALTIME)
			return clock_supported(a0) ? -EPERM : -EINVAL;
		if (!a1)
			return -EFAULT;
		if (!services_all || !(user = services_all->user) ||
		    !user->copy_from_user || !user->copy_to_user || !ops ||
		    !ops->counter_read || !ops->counter_frequency)
			return -ENOSYS;
		for (i = 0; i < sizeof(tx); i++)
			tx[i] = 0;
		if (user->copy_from_user(tx, a1, sizeof(tx)) != 0)
			return -EFAULT;
		modes = (uint32_t)tx[0] | ((uint32_t)tx[1] << 8) |
			((uint32_t)tx[2] << 16) | ((uint32_t)tx[3] << 24);
		if (modes & ~IR0_EARLY_ADJ_KNOWN)
			return -EOPNOTSUPP;
		frequency = ops->counter_frequency();
		if (!frequency)
			return -EINVAL;
		now_sec = (int64_t)(ops->counter_read() / frequency) +
			  g_realtime_off;
		now_nsec = (int64_t)(((ops->counter_read() % frequency) *
				      NS_PER_SEC) / frequency) +
			   g_realtime_nsec;
		if (now_nsec >= (int64_t)NS_PER_SEC)
		{
			now_sec++;
			now_nsec -= (int64_t)NS_PER_SEC;
		}
		if (modes & IR0_EARLY_ADJ_SETOFFSET)
		{
			int64_t dsec = 0;
			int64_t dfrac = 0;
			unsigned b;

			for (b = 0; b < 8; b++)
				dsec |= (int64_t)tx[72 + b] << (8 * b);
			for (b = 0; b < 8; b++)
				dfrac |= (int64_t)tx[80 + b] << (8 * b);
			if (modes & IR0_EARLY_ADJ_NANO)
			{
				if (dfrac <= -((int64_t)NS_PER_SEC) ||
				    dfrac >= (int64_t)NS_PER_SEC)
					return -EINVAL;
			}
			else
			{
				if (dfrac <= -1000000 || dfrac >= 1000000)
					return -EINVAL;
				dfrac *= 1000;
			}
			g_realtime_off += dsec;
			g_realtime_nsec += dfrac;
			while (g_realtime_nsec >= (int64_t)NS_PER_SEC)
			{
				g_realtime_off++;
				g_realtime_nsec -= (int64_t)NS_PER_SEC;
			}
			while (g_realtime_nsec < 0)
			{
				g_realtime_off--;
				g_realtime_nsec += (int64_t)NS_PER_SEC;
			}
		}
		else if (modes & IR0_EARLY_ADJ_OFFSET)
		{
			int64_t off = 0;
			unsigned b;

			for (b = 0; b < 8; b++)
				off |= (int64_t)tx[8 + b] << (8 * b);
			if (modes & IR0_EARLY_ADJ_NANO)
				g_realtime_nsec += off;
			else
				g_realtime_nsec += off * 1000;
			while (g_realtime_nsec >= (int64_t)NS_PER_SEC)
			{
				g_realtime_off++;
				g_realtime_nsec -= (int64_t)NS_PER_SEC;
			}
			while (g_realtime_nsec < 0)
			{
				g_realtime_off--;
				g_realtime_nsec += (int64_t)NS_PER_SEC;
			}
		}
		for (i = 0; i < 8; i++)
			tx[72 + i] = (uint8_t)((uint64_t)now_sec >> (8 * i));
		{
			int64_t usec = now_nsec / 1000;

			for (i = 0; i < 8; i++)
				tx[80 + i] = (uint8_t)((uint64_t)usec >> (8 * i));
		}
		tx[40] = IR0_EARLY_TIME_OK;
		tx[41] = 0;
		tx[42] = 0;
		tx[43] = 0;
		if (user->copy_to_user(a1, tx, sizeof(tx)) != 0)
			return -EFAULT;
		return IR0_EARLY_TIME_OK;
	}
	case IR0_SYSCALL_CLOCK_GETTIME:
		return early_clock_gettime(opaque, a0, a1);
	case IR0_SYSCALL_CLOCK_NANOSLEEP:
		if (!clock_supported(a0)) return -EINVAL;
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
	case IR0_SYSCALL_GETITIMER:
	case IR0_SYSCALL_SETITIMER:
	{
		const struct early_user_services *user;
		unsigned which = (unsigned)a0;

		if (which > 2)
			return -EINVAL;
		if (!services_all || !(user = services_all->user))
			return -ENOSYS;
		if (id == IR0_SYSCALL_GETITIMER)
		{
			if (!a1)
				return -EFAULT;
			if (!user->copy_to_user)
				return -ENOSYS;
			return user->copy_to_user(a1, &g_itimer[which],
						  sizeof(g_itimer[0])) == 0 ?
				       0 :
				       -EFAULT;
		}
		if (a2)
		{
			if (!user->copy_to_user)
				return -ENOSYS;
			if (user->copy_to_user(a2, &g_itimer[which],
					       sizeof(g_itimer[0])) != 0)
				return -EFAULT;
		}
		if (!a1)
			return 0;
		if (!user->copy_from_user)
			return -ENOSYS;
		return user->copy_from_user(&g_itimer[which], a1,
					    sizeof(g_itimer[0])) == 0 ?
			       0 :
			       -EFAULT;
	}
	case IR0_SYSCALL_CLOCK_GETRES:
	{
		struct timespec res;
		const struct early_user_services *user;
		uint64_t frequency;
		uint64_t ns;

		if (!clock_supported(a0))
			return -EINVAL;
		if (!a1)
			return 0;
		if (!services_all || !services_all->user ||
		    !(user = services_all->user) || !user->copy_to_user ||
		    !ops || !ops->counter_frequency)
			return -ENOSYS;
		frequency = ops->counter_frequency();
		if (!frequency)
			return -EINVAL;
		ns = NS_PER_SEC / frequency;
		if (!ns)
			ns = 1;
		res.tv_sec = 0;
		res.tv_nsec = (int64_t)ns;
		return user->copy_to_user(a1, &res, sizeof(res)) == 0 ? 0 : -EFAULT;
	}
	default:
		return -ENOSYS;
	}
}

int64_t early_time_realtime_offset(void)
{
	return g_realtime_off;
}

int64_t early_time_set_realtime(void *opaque, int64_t sec, int64_t nsec)
{
	const struct early_syscall_services *services_all = services(opaque);
	const struct early_time_services *ops = services_all ? services_all->time : 0;
	uint64_t frequency;
	int64_t now;

	if (sec < 0 || nsec < 0 || nsec >= (int64_t)NS_PER_SEC)
		return -EINVAL;
	if (!ops || !ops->counter_read || !ops->counter_frequency)
		return -ENOSYS;
	frequency = ops->counter_frequency();
	if (!frequency)
		return -EINVAL;
	now = (int64_t)(ops->counter_read() / frequency);
	g_realtime_off = sec - now;
	g_realtime_nsec = nsec;
	return 0;
}

int early_time_smoke_ok(void)
{
	return g_nanosleep_ok && g_clock_gettime_ok && g_gettimeofday_ok &&
	       g_clock_nanosleep_ok;
}

static const enum ir0_syscall_id g_time_syscalls[] = {
	IR0_SYSCALL_NANOSLEEP, IR0_SYSCALL_CLOCK_GETTIME,
	IR0_SYSCALL_CLOCK_NANOSLEEP, IR0_SYSCALL_GETTIMEOFDAY,
	IR0_SYSCALL_CLOCK_GETRES, IR0_SYSCALL_GETITIMER,
	IR0_SYSCALL_SETITIMER, IR0_SYSCALL_CLOCK_SETTIME,
	IR0_SYSCALL_SETTIMEOFDAY, IR0_SYSCALL_CLOCK_ADJTIME,
	IR0_SYSCALL_ADJTIMEX,
};

const struct syscall_context_provider early_time_provider = {
	.ids = g_time_syscalls,
	.count = sizeof(g_time_syscalls) / sizeof(g_time_syscalls[0]),
	.handler = early_time_syscall,
};
