# IR0 Scheduling

> **Última verificación:** 2026-11-07
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/backend.h`,
> `sched/backend_select.c`, `sched/rr/backend.c`, `sched/prio/backend.c`,
> `sched/sched_switch.c`, `sched/sched_resched.c`,
> `kernel/time/clock_wait.c`, `kernel/syscalls/io_syscalls.c`

IR0 scheduling is selected through a portable facade and a backend ops table
configured from Kconfig.

## Selection Model

- Portable API: [`includes/ir0/sched.h`](../includes/ir0/sched.h) (`sched_add_process`,
  `sched_schedule_next`, `sched_count_runnable`, …).
- Dispatch: [`sched/sched.c`](../sched/sched.c) calls `sched_backend_active()` and
  nowhere names a concrete policy.
- Selection: [`sched/backend_select.c`](../sched/backend_select.c) maps
  `CONFIG_SCHEDULER_POLICY` to the active ops table at build time.
- Backend ops: [`sched/backend.h`](../sched/backend.h) is the private contract;
  [`sched/rr/backend.h`](../sched/rr/backend.h) and
  [`sched/prio/backend.h`](../sched/prio/backend.h) export one table each.
  Backends must not `#include` or call each other's private symbols.
- Shared switch helper: [`sched/sched_switch.c`](../sched/sched_switch.c)
  (`sched_context_switch_to`) — not a runqueue API.

### Policies

| `CONFIG_SCHEDULER_POLICY` | Name (`sched_active_policy_name`) | Backend object |
|---------------------------|-----------------------------------|----------------|
| `0` | `round_robin` | `sched/rr/backend.o` |
| `1` | `cfs` | **Same RR backend object** (honest alias — no CFS runqueue) |
| `2` (defconfig) | `priority` | `sched/prio/backend.o` only |

The priority backend reports its boot selftest through `ops->selftest`;
`kernel/boot_init.c` prints `SCHED_POLICY_OK` or `SCHED_POLICY_FAIL` without
referencing a policy symbol.

## Blocking syscalls (poll / pause / nanosleep)

Blocked waiters use `process_arm_kernel_syscall_sleep` + `PROCESS_BLOCKED` and
[`kernel/time/clock_wait.c`](../kernel/time/clock_wait.c):

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

- Portable callers never include `<sched/backend.h>`, `<sched/rr/backend.h>`, or
  peer backends.
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
- Class B gates: `make -C tests/host run` (`test_class_b_ctx_invariant`) and the KTM Class B scenario.
