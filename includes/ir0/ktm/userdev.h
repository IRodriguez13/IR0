/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: userdev.h
 * Description: /dev/ktm registration hook (CONFIG_KTM_USERDEV).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <config.h>

#if defined(CONFIG_KTM_USERDEV) && CONFIG_KTM_USERDEV
void ktm_userdev_register(void);
#else
static inline void ktm_userdev_register(void)
{
}
#endif
