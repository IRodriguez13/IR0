/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: timerfd.h
 * Description: timerfd CLOCK_MONOTONIC MVP (create/settime/gettime).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <ir0/types.h>
#include <ir0/time.h>

#define IR0_TFD_CLOEXEC  0x80000u
#define IR0_TFD_NONBLOCK 0x800u
#define IR0_CLOCK_MONOTONIC 1

struct ir0_timerfd;

int64_t sys_timerfd_create(int clockid, int flags);
int64_t sys_timerfd_settime(int fd, int flags, const struct itimerspec *new_value,
			    struct itimerspec *old_value);
int64_t sys_timerfd_gettime(int fd, struct itimerspec *curr_value);
int ir0_timerfd_is(const void *ptr);
int64_t ir0_timerfd_read(struct ir0_timerfd *t, void *buf, size_t count, int nonblock);
int ir0_timerfd_poll_readable(struct ir0_timerfd *t);
void ir0_timerfd_acquire(struct ir0_timerfd *t);
void ir0_timerfd_release(struct ir0_timerfd *t);
