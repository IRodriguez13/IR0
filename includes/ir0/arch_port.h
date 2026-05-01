/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * IR0 Kernel — Core system software
 * Copyright (C) 2025  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 *
 * Portable architecture façade for callers that must not embed
 * `<arch/common/...>` include paths directly (filesystem pseudo-nodes).
 *
 * Delegates to `arch/common/arch_portable.h` for CPU/feature queries.
 */

#pragma once

#include <arch/common/arch_portable.h>
