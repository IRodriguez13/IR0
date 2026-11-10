# IR0 Scheduling

> **Última verificación:** 2026-11-09
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`,
> `sched/rr.c`, `sched/prio.c`,
> `kernel/time/clock_wait.c`, `kernel/syscalls/io_syscalls.c`

IR0 scheduling is one core plus one policy file chosen at build time from
Kconfig. There is no run-time dispatch.

## Selection Model

- Portable API: [`includes/ir0/sched.h`](../includes/ir0/sched.h):
  `schedule()`, `sched_switch_to()`, `sched_take_resched()`, `sched_add()`,
  `sched_remove()`, `sched_promote()`, `sched_count_runnable()`,
  `sched_policy_name()`, `sched_selftest()`.
- Core: [`sched/core.c`](../sched/core.c) owns `schedule()` (pick, then
  switch), the context switch and the reschedule check.
- Policy contract: [`sched/core.h`](../sched/core.h) (private to `sched/`).
  A policy implements the queue operations of `<ir0/sched.h>` plus
  `sched_pick_next()`. Policies never include or call each other.
- Runqueue lock: `rq_lock()` / `rq_unlock()` in `sched/core.h`. On UP they
  mask local interrupts; see "Multi-CPU boundary" below.

### Policies

| `CONFIG_SCHEDULER_POLICY` | Name (`sched_policy_name`) | Policy object |
|---------------------------|-----------------------------------|----------------|
| `0` | `round_robin` | `sched/rr.o` |
| `1` | `cfs` | **Same RR object** (honest alias — no CFS runqueue) |
| `2` (defconfig) | `priority` | `sched/prio.o` only |

The Makefile links exactly one policy object. `sched_selftest()` checks the
priority bands (round robin has nothing to check and returns 0);
`kernel/boot_init.c` prints `SCHED_POLICY_OK` or `SCHED_POLICY_FAIL`.

### Multi-CPU boundary

IR0 is UP. Everything that only exists with several CPUs (CPU bring-up,
IPIs, per-CPU areas, cross-CPU calls, runqueue load balancing) belongs in a
top-level `smp/` directory, created with the first real multi-CPU code. The
scheduler core and the policies only see `rq_lock()` / `rq_unlock()`; an SMP
build implements that pair with the per-CPU runqueue spinlock from `smp/`
without touching `sched/`. Today's `kernel/percpu.c` is single-CPU storage
and stays in `kernel/`.

## Blocking syscalls (poll / pause / nanosleep)

Blocked waiters use `process_arm_kernel_syscall_sleep` + `PROCESS_BLOCKED` and
[`kernel/time/clock_wait.c`](../kernel/time/clock_wait.c):

- `ir0_clock_wait_block_until` — nanosleep / `poll(NULL,0,timeout)`.
- `ir0_clock_wait_service_runqueue` — poll/pause loops.
- Idle steps call `kernel_idle_poll_nosched()` so wake checks (`poll_wake_check_nosched`,
  stdin, pipes) **do not** nest `schedule`; the wait loop owns a single yield.

IRQ timer path uses `poll_wake_check_nosched` + deferred resched (`sched_take_resched()`).

## Class B

See [`KTM.md`](KTM.md) (Class B context). Product repair: `IR0_CLASS_B_REPAIR=1`.

## Runtime Characteristics

- Scheduling integrates with process and signal handling.
- Queue mutation paths are serialized by `rq_lock()`.
- Context switch follows Linux: `schedule()` → `sched_switch_to()` →
  `switch_to(prev, next)` → arch `__switch_to()`. Every task sleeps on its
  own kernel stack and continues when `switch_to()` returns; userspace is
  re-entered only through the task's syscall/exception exit (first run:
  `switch_to_user_task()`, Linux `ret_from_fork`). `switch_to(NULL, next)`
  is the first transfer from boot.

## Strengths

- Portable callers never include `sched/` headers.
- Policy selection is Makefile object selection.
- Priority bands are a real separate policy (not a wrapper of RR).
- Blocked syscall sleeps yield without nested schedule storms.

## Weak Points

- Policy `1` is not a fair CFS runqueue — name only until a real CFS policy
  file implements the `sched/core.h` contract.
- SMP-oriented scheduling is not the current baseline.
- Timer preemption in IRQ remains gated / deferred (see mandocs).

## Related

- Mandocs: [`mandocs/en/scheduler.md`](mandocs/en/scheduler.md)
- Decoupling map: [`DECOUPLING.md`](DECOUPLING.md)
- Class B gates: `make -C tests/host run` (`test_class_b_ctx_invariant`) and the KTM Class B scenario.
