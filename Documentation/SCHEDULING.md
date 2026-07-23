# IR0 Scheduling

> **Última verificación:** 2026-07-23
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/rr_sched.c`,
> `sched/priority_sched.c`, `sched/sched_switch.c`, `sched/sched_resched.c`,
> `kernel/clock_wait.c`, `kernel/syscalls/io_syscalls.c`

IR0 scheduling is selected through a portable facade and a backend ops table
configured from Kconfig.

## Selection Model

- Portable API: [`includes/ir0/sched.h`](../includes/ir0/sched.h) (`sched_add_process`,
  `sched_schedule_next`, `sched_count_runnable`, …).
- Dispatch: [`sched/sched.c`](../sched/sched.c) holds a `const struct ir0_sched_ops *`
  selected at build time (`CONFIG_SCHEDULER_POLICY`).
- Backend ops: [`sched/sched_ops.h`](../sched/sched_ops.h) — RR and priority each export
  their own `ir0_*_sched_ops` table. Backends must not `#include` each other.
- Shared switch helper: [`sched/sched_switch.c`](../sched/sched_switch.c)
  (`sched_context_switch_to`) — not a runqueue API.
- Compat: `includes/ir0/scheduler_api.h` and `sched/scheduler_api.h` only
  `#include <ir0/sched.h>`.

### Policies

| `CONFIG_SCHEDULER_POLICY` | Name (`sched_active_policy_name`) | Backend object |
|---------------------------|-----------------------------------|----------------|
| `0` | `round_robin` | `sched/rr_sched.o` |
| `1` | `cfs` | **Same RR ops** (honest alias — no `cfs_sched.c`, no CFS runqueue) |
| `2` (defconfig) | `priority` | `sched/priority_sched.o` only |

## Blocking syscalls (poll / pause / nanosleep)

Blocked waiters use `process_arm_kernel_syscall_sleep` + `PROCESS_BLOCKED` and
[`kernel/clock_wait.c`](../kernel/clock_wait.c):

- `ir0_clock_wait_block_until` — nanosleep / `poll(NULL,0,timeout)`.
- `ir0_clock_wait_service_runqueue` — poll/pause loops.
- Idle steps call `kernel_idle_poll_nosched()` so wake checks (`poll_wake_check_nosched`,
  stdin, pipes) **do not** nest `sched_schedule_next`; the wait loop owns a single yield.

IRQ timer path uses `poll_wake_check_nosched` + deferred resched (`sched_resched.c`).
IRQ preempt that already saved the user iretq frame sets
`sched_context_switch_skip_prev_save()` so `switch_context_x64` does not overwrite it
with mid-ISR kernel CS/RIP (Class B close).

## Class B

See [`KTM.md`](KTM.md) (Class B context). Product repair: `IR0_CLASS_B_REPAIR=1`.

## Runtime Characteristics

- Scheduling integrates with process and signal handling.
- Queue mutation paths are IRQ-serialized per backend.
- Context-switch assembly remains architecture-specific behind
  **`first_switch_to`** and **`switch_to`** (ISA asm private).

## Strengths

- Portable callers never include `<sched/rr_sched.h>` or peer backends.
- Policy iteration is ops-table / Makefile object selection.
- Priority bands are a real separate backend (not a wrapper of RR headers).
- Blocked syscall sleeps yield without nested schedule storms.

## Weak Points

- Policy `1` is not a fair CFS runqueue — name only until a real CFS backend
  registers its own ops without including RR.
- SMP-oriented scheduling is not the current baseline.
- Timer preemption in IRQ remains gated / deferred (see mandocs).

## Related

- Mandocs: [`mandocs/en/scheduler.md`](mandocs/en/scheduler.md)
- Decoupling map: [`DECOUPLING.md`](DECOUPLING.md)
- Class B gates: `scripts/make/class-b.mk`
