# IR0 Scheduler

> **Última verificación:** 2026-07-17
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/sched_ops.h`, `sched/rr_sched.c`, `sched/priority_sched.c`

| Field | Value |
|-------|-------|
| Version | 0.3 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | process, memory |
| Man page | IR0-scheduler (section 7) |
| Primary sources | `includes/ir0/sched.h`, `sched/sched.c`, `sched/sched_switch.c`, `sched/priority_sched.c`, `sched/rr_sched.c`, `arch/common/arch_portable.h` (`arch_first_context_switch`), `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c` |

## 1. Overview

IR0 schedules runnable processes through a **portable facade** and a
**backend ops table** selected by `CONFIG_SCHEDULER_POLICY`.
Default defconfig uses **priority bands** (`CONFIG_SCHEDULER_POLICY=2`).
Round-robin (`0`) and CFS-name alias (`1`) remain available. Scheduling is
**single-CPU** oriented; timer-driven preemption is currently deferred in the
PIT handler.

The first transfer into a newly runnable task goes through
`arch_first_context_switch(next)` (x86: `user_mode.c`; ARM: `first_switch.c`).
Portable backend code must **not** embed `iretq` / ISA entry frames.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `includes/ir0/sched.h` | Portable API (no `<sched/…>` includes) |
| `sched/sched.c` | Holds active `ir0_sched_ops`; dispatches add/remove/schedule/count/promote |
| `sched/sched_ops.h` | Ops struct; backends export `ir0_rr_sched_ops` / `ir0_priority_sched_ops` |
| `sched/sched_switch.c` | Shared `sched_context_switch_to` (not a runqueue) |
| `sched/sched_resched.c` | IRQ/TTY/user-return resched helpers |
| `priority_sched.c` | Default bands when `CONFIG_SCHEDULER_POLICY=2` |
| `rr_sched.c` | Circular FIFO queue when policy `0` or `1` |
| `process_t::state` | `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE` |
| `arch_first_context_switch` | First user/kernel entry (arch-owned) |
| `arch_context_switch.c` | Later switches → `switch_context_x64` / ARM path |

**Policy `1` (name `cfs`):** uses **RR ops** with a different policy name. There is
no `cfs_sched.c` and no `#include "rr_sched.h"` from a fake CFS wrapper.

## 3. Data flow

**`sched_schedule_next()` (via ops):**

1. CLI (IRQ-safe) inside the active backend.
2. If queue empty → return (idle loop handles HLT).
3. Pick next runnable; skip `ZOMBIE` and `BLOCKED`.
4. Call `sched_context_switch_to(next)`.
5. First switch: `arch_first_context_switch(next)`.
6. Later: `arch_context_switch(&prev->task, &next->task)`.

**Wake paths** set `PROCESS_READY` and may call `sched_schedule_next`
(poll/sleep/stdin/pipe/IPC).

## 4. Responsibilities

- Scheduler picks next runnable task; does not implement syscall blocking.
- Blocked processes stay off the run queue until wake sets `PROCESS_READY`.
- Arch owns first-entry and context-switch ISA details.

## 5. Subsystem boundaries

- Portable code calls `sched_*` only via `includes/ir0/sched.h`.
- Backends must not include peer runqueue headers (priority must not include
  `rr_sched.h`).
- No inline `iretq` in portable sched code.
- `scheduler_api.h` is a compat alias of `sched.h`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Process | `sched_add_process` / `sched_remove_process` on spawn/exit |
| Syscalls | Block in handler; wake from idle poll |
| Timer | PIT quantum counting (preemption hook deferred) |
| Arch | `arch_first_context_switch`, `switch_context_x64` / ARM |

## 7. Invariants

1. `process_t` begins with embedded `task_t` at offset 0.
2. Duplicate `sched_add_process` is idempotent.
3. Zombies are skipped by pickers.
4. First entry always goes through the arch facade.

## 8. Future

- Real CFS fair backend registering its own ops (no RR include).
- Re-enable timer quantum preemption when iretq path is hardened.
- SMP runqueues — not implemented.
