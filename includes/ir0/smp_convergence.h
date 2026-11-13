/**
 * IR0 Kernel — Core system software
 * Copyright (C) 2026  Iván Rodriguez
 *
 * File: smp_convergence.h
 * Description: Linux alignment notes for future SMP / scalable drivers (design only).
 */

/* SPDX-License-Identifier: GPL-3.0-only */

#pragma once

/*
 * Use this header as a single place for "what to read in Linux" when growing
 * IR0 toward SMP and heavier networking — not as a runtime API.
 *
 * Linux (reference trees — concepts, not copy-paste):
 * - kernel/smp.c, kernel/cpu.c — boot CPU, bring-up order, notifier-style hooks
 * - kernel/sched/fair.c, kernel/sched/core.c — runqueue ownership, migration
 * - arch/*/kernel/smp*.c — IPIs, TLB shootdown, crash other CPUs
 * - kernel/locking/spinlock.c, include/linux/spinlock*.h — raw vs irqsave
 * - net/core/dev.c, drivers/net/* — netdev ops, NAPI, softirq budget
 *
 * IR0 mapping (facades and boundaries — implement incrementally):
 * - Barriers: smp_mb(), smp_rmb(), smp_wmb() in ir0/cpu.h backends
 * - Topology: cpu_id() / cpu_online_* in ir0/cpu.h; no raw APIC/GIC from mm/fs/net
 * - Critical sections: ir0/spinlock.h → irq_save/restore today; CONFIG_SMP path = atomics + IPI
 * - Cross-CPU MM: TLB flush via arch facade after pmap update (see mm/paging/tlb_sync.c)
 * - Drivers: register through ir0/driver.h / block_dev / netdev tables — no arch/ in fs/
 *
 * CONFIG_ENABLE_SMP (version.h): build gate only — runtime SMP-safe paths must
 * be proven with ktest/smoke before enabling.
 */
