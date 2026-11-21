/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * This file is part of the IR0 Operating System.
 * Distributed under the terms of the GNU General Public License v3.0.
 * See the LICENSE file in the project root for full license information.
 *
 * File: arch_port.h
 * Description: Portable arch facade — no <arch/...> includes (decls under ir0/).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

#include <ir0/early_clock.h>
#include <ir0/tlb.h>
#include <ir0/tls.h>
#include <ir0/cpu_info.h>
#include <ir0/irq.h>
#include <asm/pgtable.h>
#include <ir0/page_fault.h>
#include <asm/setup.h>
#include <ir0/cpu.h>
#include <ir0/context.h>
