/**
 * IR0 userspace — libktm-user
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: libktm_user.c
 * Description: /dev/ktm client helpers.
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include "libktm_user.h"

#include <fcntl.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

int ktm_open(void)
{
	return open("/dev/ktm", O_RDWR);
}

void ktm_close(int fd)
{
	if (fd >= 0)
		(void)close(fd);
}

static int ktm_user_event(int fd, uint32_t type, uint32_t subsys,
			  uint64_t a0, uint64_t a1, uint64_t a2, uint64_t a3,
			  const char *name)
{
	ktm_user_event_t ev;

	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.subsystem = subsys;
	ev.arg0 = a0;
	ev.arg1 = a1;
	ev.arg2 = a2;
	ev.arg3 = a3;
	if (name)
	{
		strncpy(ev.name, name, sizeof(ev.name) - 1);
		ev.name[sizeof(ev.name) - 1] = '\0';
	}
	return ioctl(fd, KTM_IOC_USER_EVENT, &ev);
}

int ktm_case_begin(int fd, const char *name)
{
	return ktm_user_event(fd, KTM_UAPI_EVENT_CASE_BEGIN, KTM_UAPI_SUBSYS_TEST,
			      0, 0, 0, 0, name);
}

int ktm_case_end(int fd, const char *name, int result)
{
	return ktm_user_event(fd, KTM_UAPI_EVENT_CASE_END, KTM_UAPI_SUBSYS_TEST,
			      (uint64_t)(result == 0 ? 0 : 1), 0, 0, 0, name);
}

int ktm_assert_eq_u64(int fd, const char *name, uint64_t expected, uint64_t actual)
{
	int ok = (expected == actual);

	return ktm_user_event(fd, KTM_UAPI_EVENT_USER_ASSERT, KTM_UAPI_SUBSYS_TEST,
			      (uint64_t)(ok ? 1 : 0), expected, actual, 0, name);
}

int ktm_assert_true(int fd, const char *name, int cond)
{
	return ktm_user_event(fd, KTM_UAPI_EVENT_USER_ASSERT, KTM_UAPI_SUBSYS_TEST,
			      (uint64_t)(cond ? 1 : 0), 0, 0, 0, name);
}

int ktm_checkpoint(int fd, const char *name)
{
	return ktm_user_event(fd, KTM_UAPI_EVENT_CHECKPOINT, KTM_UAPI_SUBSYS_TEST,
			      0, 0, 0, 0, name);
}

int ktm_snapshot_request(int fd, ktm_ioc_snapshot_t *out)
{
	ktm_ioc_snapshot_t snap;
	int rc;

	memset(&snap, 0, sizeof(snap));
	rc = ioctl(fd, KTM_IOC_TAKE_SNAPSHOT, &snap);
	if (rc == 0 && out)
		*out = snap;
	return rc;
}

int ktm_run_invariants(int fd, uint32_t mask)
{
	ktm_user_invariants_t inv;

	memset(&inv, 0, sizeof(inv));
	inv.mask = mask;
	return ioctl(fd, KTM_IOC_RUN_INVARIANTS, &inv);
}

int ktm_run_scenario(int fd, const char *name, int32_t *result_out)
{
	ktm_user_scenario_t sc;
	int rc;

	memset(&sc, 0, sizeof(sc));
	if (name)
	{
		strncpy(sc.name, name, sizeof(sc.name) - 1);
		sc.name[sizeof(sc.name) - 1] = '\0';
	}
	rc = ioctl(fd, KTM_IOC_RUN_SCENARIO, &sc);
	if (result_out)
		*result_out = sc.result;
	return rc;
}

int ktm_get_caps(int fd, ktm_user_caps_t *out)
{
	ktm_user_caps_t caps;
	int rc;

	memset(&caps, 0, sizeof(caps));
	rc = ioctl(fd, KTM_IOC_GET_CAPS, &caps);
	if (rc == 0 && out)
		*out = caps;
	return rc;
}

int ktm_reset(int fd)
{
	return ioctl(fd, KTM_IOC_RESET, 0);
}
