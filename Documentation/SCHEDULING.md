# IR0 Scheduling

> **Última verificación:** 2026-07-17
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/sched.c`, `sched/rr_sched.c`, `sched/priority_sched.c`

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

## Runtime Characteristics

- Scheduling integrates with process and signal handling.
- Queue mutation paths are IRQ-serialized per backend.
- Context-switch assembly remains architecture-specific
  (`arch_first_context_switch`, `arch_context_switch`).

## Strengths

- Portable callers never include `<sched/rr_sched.h>` or peer backends.
- Policy iteration is ops-table / Makefile object selection.
- Priority bands are a real separate backend (not a wrapper of RR headers).

## Weak Points

- Policy `1` is not a fair CFS runqueue — name only until a real CFS backend
  registers its own ops without including RR.
- SMP-oriented scheduling is not the current baseline.
- Timer preemption in IRQ remains gated / deferred (see mandocs).

## Related

- Mandocs: [`mandocs/en/scheduler.md`](mandocs/en/scheduler.md)
- Decoupling map: [`DECOUPLING.md`](DECOUPLING.md)
