# IR0 Scheduler

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 |
| Status | stable |
| Depends on | process, memory |
| Man page | IR0-scheduler (section 7) |
| Primary sources | `sched/scheduler_api.c`, `sched/rr_sched.c`, `sched/switch/switch_x64.asm`, `drivers/timer/clock_system.c`, `kernel/main.c` |

## 1. Overview

IR0 schedules runnable processes through a config-selected policy facade.
Default build uses **round-robin** (`CONFIG_SCHEDULER_POLICY=0`). CFS and priority
backends compile but are not the default. Scheduling is **single-CPU** oriented;
timer-driven preemption is currently disabled in the PIT handler.

## 2. Internal architecture

| Piece | Role |
|-------|------|
| `scheduler_api.c` | Dispatch to active policy backend |
| `rr_sched.c` | Circular FIFO queue (`rr_head`, `rr_current`) |
| `rr_task_t` | `{ process_t *process; rr_task_t *next }` |
| `process_t::state` | `READY`, `RUNNING`, `BLOCKED`, `ZOMBIE` |
| `task_t` | ASM-visible context (CR3, RIP, RSP, segments) at offset 0 of `process_t` |
| `arch_context_switch.c` | Chooses `switch_context_x64` vs syscall-resume path |
| `switch_x64.asm` | Saves GPRs, CR3, user `iretq` frame |

## 3. Data flow

**`sched_schedule_next()` (RR):**

1. CLI (IRQ-safe).
2. If queue empty → return (idle loop handles HLT).
3. Advance `rr_current` circularly; skip `ZOMBIE` and `BLOCKED` (max 100 tries).
4. Mark prev `RUNNING→READY`, next `READY→RUNNING`; `current_process = next`.
5. First switch: load CR3; user → `arch_switch_to_user`; kernel task → inline iretq.
6. Later: `arch_context_switch(&prev->task, &next->task)`.

**Wake paths (set `PROCESS_READY`, may call `sched_schedule_next`):**

```text
  blocked syscall ──► PROCESS_BLOCKED
                           │
  poll_wake_check ─────────┤ idle: kernel_idle_poll()
  sleep_wake_check ────────┤
  stdin_wake_check ────────┤ keyboard IRQ
  pipe_wake_* ─────────────┤
  IPC wake ────────────────┘
                           ▼
                    PROCESS_READY → sched_schedule_next
```

**Timer (`clock_system.c`):** PIT tick increments quantum counter; **`sched_schedule_next` in IRQ is `#if 0`** — preemption deferred until iretq hardening complete.

## 4. Responsibilities

- Scheduler picks next runnable task; does not implement syscall blocking.
- Blocked processes stay off RR queue until wake sets `PROCESS_READY`.
- Idle kernel task (`comm "idle"`) re-enqueued on user exit; kept off queue while PID1 runs (see `process.c` comments).

## 5. Subsystem boundaries

- Must not `HLT` inside `rr_schedule_next` (IF=0 in IRQ context).
- Context switch ASM owns `task_t` layout; changing offsets requires ASM update.
- Portable code calls `sched_schedule_next()` via `includes/ir0/scheduler_api.h`.

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Process | `sched_add_process` / `sched_remove_process` on spawn/exit |
| Syscalls | Block in handler; wake from idle poll |
| Timer | PIT quantum counting (preemption hook disabled) |
| Arch | `switch_context_x64`, `arch_switch_to_user` |

## 7. Visual maps

```text
       ┌─────────────┐
       │ RR queue    │◄── sched_add_process
       └──────┬──────┘
              │ sched_schedule_next
              ▼
       ┌─────────────┐     ┌──────────────────┐
       │ RUNNING     │────►│ switch_context   │
       └─────────────┘     │ _x64 / sysret    │
              ▲            └──────────────────┘
              │ wake
       ┌──────┴──────┐
       │ BLOCKED     │
       └─────────────┘
```

State transitions:

```text
  READY ──pick──► RUNNING ──block──► BLOCKED
    ▲                  │                │
    └──── wake ────────┘                │
    ▲                                     │
    └─────────── wake ────────────────────┘
  RUNNING ──exit──► ZOMBIE (skipped by RR)
```

## 8. Important invariants

1. `process_t` begins with embedded `task_t` at offset 0.
2. Duplicate `sched_add_process` is idempotent (marks READY, frees duplicate node).
3. RR remove is O(n) linked-list walk.
4. Zombie tasks are skipped when picking next.
5. Timer IRQ must not preempt until `#if 0` block is enabled and tested.

## 9. Debugging tips

Tags: `[FASE50][SCHED]`, `[FASE50][CTX]`, `[WAIT_EXIT_AUDIT]` (`IR0_DEBUG_WAIT`).

- If system hangs with runnable tasks: check all tasks stuck in `BLOCKED`.
- If no preemption: expected — timer path disabled; only explicit `sched_schedule_next`.
- ktest/host: scheduler-related tests under `kernel/test/` when enabled.

## 10. Future roadmap

- Re-enable timer quantum preemption in `clock_tick_handler`.
- SMP run queues and per-CPU `current_process` — **not implemented**.
- CFS/priority policies need feature parity before default switch.
- Work-conserving idle vs HLT tradeoff in `kernel_idle_poll`.
