# IR0 Scheduling

> **Última verificación:** 2026-11-09
> **Fuente de verdad:** `includes/ir0/sched.h`, `sched/core.h`, `sched/core.c`,
> `sched/idle.c`, `kernel/futex.c`,
> `sched/rr.c`, `sched/prio.c`,
> `kernel/time/clock_wait.c`, `kernel/syscalls/io_syscalls.c`,
> `kernel/syscalls/syscall_dispatch.c`, `kernel/signals.c`

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

## Blocking syscalls and signals

A blocking syscall sleeps on its own kernel stack (Linux `wait_event`):
it sets `PROCESS_BLOCKED`, re-tests its condition and `signal_pending()`,
and calls `schedule()`. A wake source sets `PROCESS_READY`; so does
`send_signal()` for a blocked target. A sleep ended by a signal returns
`-ERESTARTSYS` (read/write on pipes and ttys, wait4, futex),
`-ERESTARTNOHAND` (pause, rt_sigsuspend) or `-EINTR` (nanosleep with the
remaining time, poll, select).

An interrupt that came from userspace runs the same exit work
(`irqentry_exit_to_user_mode()` in [`kernel/entry.c`](../kernel/entry.c),
Linux `irqentry_exit_to_user_mode()`), called by the ISA after the EOI and
`cpu_local_irq_leave()` (x86 `isr_handler64`, arm64 `arm64_exc_irq_el1`):
a pending reschedule sleeps in `schedule()` (a task spinning in userspace
is preempted at the timer quantum), and a deliverable signal goes through
`do_signal()` with the interrupted context, then the task returns through
its full user state as `rt_sigreturn` does.

The syscall exit (`exit_to_user_mode()` in `syscall_dispatch.c`, Linux
`syscall_exit_to_user_mode()`) stores the result in the task's syscall
frame, sleeps in `schedule()` when a reschedule is pending, and calls
`do_signal()`: it restarts the call (`syscall_frame_restart()`) or turns it
into `EINTR` per `SA_RESTART`, and points the frame at the handler. The ISA
exit then returns through that frame (`syscall_restore_exit_regs()`).

No sleeper polls. The producer wakes it: the timer IRQ fires clock and
poll timeouts ([`kernel/time/clock_wait.c`](../kernel/time/clock_wait.c),
`ir0_clock_wait_fire_due()` from `clock_tick()`), pipe I/O and close call
`pipe_wake_all()`, the keyboard path wakes tty and stdin readers,
`ir0_futex_wake()` wakes futex waiters (the waiter lives on the sleeper's
kernel stack, Linux `futex_q`). Locks that may sleep (the VFS fs-op lock)
queue the contender on a wait queue. `clock_sleep()` is the
`msleep_interruptible()` of drivers; `udelay()` (`<ir0/delay.h>`) is the
bounded hardware delay.

## Idle task

[`sched/idle.c`](../sched/idle.c) runs `kernel_idle_loop()`: enable IRQs,
poll the devices that may not interrupt (NIC, Bluetooth HCI UART, PS/2;
builds without networking or Bluetooth link no-op stubs, so there is no
`#if`), run a woken task if one is READY, otherwise halt in `cpu_idle()`
until the next interrupt. Sleeping tasks never run the idle poll.

`cpu_relax()` is retired. The only low-power stop besides `cpu_idle()` is
the terminal `cpu_halt()` in `<ir0/oops_port.h>` (panic, failed power-off,
returning kexec); arch-guard `[no-busy-wait]` rejects any other caller.

## Runtime Characteristics

- Scheduling integrates with process and signal handling.
- Queue mutation paths are serialized by `rq_lock()`.
- Context switch follows Linux: `schedule()` → `sched_switch_to()` →
  `switch_to(prev, next)` → arch `__switch_to()`. Every task sleeps on its
  own kernel stack and continues when `switch_to()` returns; userspace is
  re-entered only through the task's syscall/exception exit (first run:
  `switch_to_user_task()`, Linux `ret_from_fork`). `switch_to(NULL, next)`
  is the first transfer from boot.
- FP/SIMD registers are per-task state (Linux x86 `fpu`, arm64
  `fpsimd`). The kernel is built without FP/SIMD code, so the live
  registers always belong to the current task: the arch `__switch_to()`
  saves them into the outgoing task and loads the incoming one's
  (eager switching, `arch/<isa>/sources/fpu.c` behind
  `<ir0/fpu_backend.h>`). A fork child starts with its parent's registers,
  `execve` resets them (`fpu_flush_thread()`), and the signal frame keeps
  the interrupted registers for `rt_sigreturn` (x86-64: the FXSAVE image;
  arm64: an `fpsimd_context` record).

## Strengths

- Portable callers never include `sched/` headers.
- Policy selection is Makefile object selection.
- Priority bands are a real separate policy (not a wrapper of RR).
- Blocked syscall sleeps yield without nested schedule storms.

## Weak Points

- Policy `1` is not a fair CFS runqueue — name only until a real CFS policy
  file implements the `sched/core.h` contract.
- SMP-oriented scheduling is not the current baseline.

## Related

- Mandocs: [`mandocs/en/scheduler.md`](mandocs/en/scheduler.md)
- Decoupling map: [`DECOUPLING.md`](DECOUPLING.md)
