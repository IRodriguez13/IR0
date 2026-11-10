# IR0 Scheduler

> **Última verificación:** 2026-11-09
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`, `sched/rr.c`, `sched/prio.c`

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | process, memory |
| Man page | IR0-scheduler (section 7) |
| Primary sources | `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`, `sched/rr.c`, `sched/prio.c`, `sched/switch/context_switch.c`, `includes/ir0/context_backend.h`, `arch/x86-64/asm/context/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Overview

IR0 schedules runnable processes through a **portable facade**, one
**scheduler core** and **one policy file** linked by `CONFIG_SCHEDULER_POLICY`.
Default defconfig uses **priority bands** (`CONFIG_SCHEDULER_POLICY=2`).
Round-robin (`0`) and CFS-name alias (`1`) remain available. Scheduling is
**single-CPU** oriented; timer-driven preemption is currently deferred in the
PIT handler.

The first transfer into a newly runnable task goes through
`switch_to(NULL, next)`, which hands over to the arch hook
`first_switch_to()` (x86: `user_mode.c`; ARM: `first_switch.c`).
Portable backend code must **not** embed `iretq` / ISA entry frames.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `includes/ir0/sched.h` | Portable API (no `<sched/…>` includes) |
| `sched/core.c` | `schedule()`, `sched_switch_to()`, `sched_take_resched()` |
| `sched/core.h` | Private core/policy contract (`sched_pick_next`, `rq_lock`/`rq_unlock`) |
| `sched/prio.c` | Default priority bands when `CONFIG_SCHEDULER_POLICY=2` |
| `sched/rr.c` | Ready list for policy `0` or `1` |
| `process_t::state` | `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE` |
| `sched/switch/context_switch.c` | `switch_to(prev, next)` → arch `__switch_to()` |
| `sched/switch/kernel_resume.c` | Blocked-syscall bookkeeping + Class B recovery before the continuation runs |
| `sched/switch/diag.c` | `switch_trace()`: the CTX CLASSIFY tags |

**Policy `1` (name `cfs`):** uses the **RR policy object** with a different
policy name. There is no `cfs_sched.c` and no fake CFS wrapper that includes
peer runqueue internals.

## 3. Data flow

**`schedule()`:**

1. `sched_pick_next()` takes `rq_lock()` inside the linked policy.
2. If queue empty → return (idle loop handles HLT).
3. Pick next runnable; skip `ZOMBIE` and `BLOCKED`.
4. The core calls `sched_switch_to(next)`.
5. First switch: `switch_to(NULL, &next->task)` (does not return).
6. Later: `switch_to(&prev->task, &next->task)`; it returns when @prev runs
   again, on its own kernel stack.

**Wake paths** set `PROCESS_READY` and may call `schedule`
(poll/sleep/stdin/pipe/IPC).

## 4. Responsibilities

- Scheduler picks next runnable task; does not implement syscall blocking.
- Blocked processes stay off the run queue until wake sets `PROCESS_READY`.
- Arch owns first-entry and context-switch ISA details.

## 5. Subsystem boundaries

- Portable code calls `sched_*` only via `includes/ir0/sched.h`.
- Policies must not include or call each other; both implement
  `sched/core.h`.
- No inline `iretq` in portable sched code.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Process | `sched_add` / `sched_remove` on spawn/exit |
| Syscalls | Block in handler; wake from idle poll |
| Timer | PIT quantum counting (preemption hook deferred) |
| Arch | `__switch_to`, `first_switch_to`, `set_current_kernel_stack` |

## 7. Invariants

1. `process_t` begins with embedded `task_t` at offset 0.
2. Duplicate `sched_add` is idempotent.
3. Zombies are skipped by pickers.
4. First entry always goes through the arch facade.

## 8. Future

- Real CFS fair policy file implementing `sched/core.h` (no RR include).
- Re-enable timer quantum preemption when iretq path is hardened.
- SMP runqueues — not implemented; multi-CPU code goes to `smp/` and
  implements `rq_lock()` / `rq_unlock()`.
