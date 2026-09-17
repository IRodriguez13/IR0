/* SPDX-License-Identifier: GPL-3.0-only */
#pragma once

#include <stdint.h>

/* Generic fasync boundary used by device-facing readiness adapters. */
void io_async_notify_device(uint32_t device_id);
