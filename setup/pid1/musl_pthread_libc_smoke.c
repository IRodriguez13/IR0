/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: musl_pthread_libc_smoke.c
 * Description: Real musl pthread_create/join smoke (-lpthread).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#include <pthread.h>
#include <unistd.h>
#include <stdint.h>

static volatile int worker_ran;

static void *worker(void *arg)
{
	(void)arg;
	worker_ran = 1;
	return (void *)(uintptr_t)42;
}

int main(void)
{
	pthread_t th;
	void *ret = NULL;

	if (pthread_create(&th, NULL, worker, NULL) != 0)
		return 1;

	if (pthread_join(th, &ret) != 0)
		return 2;

	if (!worker_ran)
		return 3;

	if ((uintptr_t)ret != 42)
		return 4;

	if (write(1, "MUSL_PTHREAD_LIBC_OK\n", 21) != 21)
		return 5;

	return 0;
}
