# IR0 Signals

| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T1 |
| Status | stable |
| Depends on | process, scheduler, interrupts |
| Man page | IR0-signals (section 7) |
| Primary sources | `includes/ir0/signals.c`, `includes/ir0/signals.h`, `kernel/syscalls.c`, `sched/rr/backend.c`, `arch/x86-64/sources/interrupt/isr_handlers.c` |

## 1. Overview

IR0 implements a subset of POSIX signals: pending bitmask, handlers, mask,
`kill`, `rt_sigaction`, `rt_sigprocmask`, `rt_sigsuspend` and `rt_sigreturn`.
Delivery follows Linux: a blocked syscall that sees `signal_pending()` returns
`-ERESTARTSYS` (or `-ERESTARTNOHAND` / `-EINTR`), and the syscall exit runs
`do_signal()`, which applies default actions, restarts the call or turns it
into `EINTR`, and sets the task's syscall frame up to enter the handler.

## 2. Internal architecture

**`process_t` fields:**

```text
  signal_pending (uint32_t bitmask)
  signal_handlers[_NSIG]  (_NSIG = 32)
  signal_mask, signal_ignored
  saved_context (for sigreturn)
```

| Function | Role |
|----------|------|
| `send_signal` | Set pending bit on target process; wake a blocked target |
| `signal_pending` | Must an interruptible sleep end? (Linux `signal_pending()`) |
| `do_signal` | Syscall exit: default actions, restart / EINTR, handler entry |
| `register_signal_handler` | Install handler; reject SIGKILL/SIGSTOP |
| Syscalls | `sys_kill`, `sys_rt_sigaction`, `sys_rt_sigprocmask`, `sys_rt_sigsuspend`, `sys_sigreturn` |

## 3. Data flow

```text
  sys_kill / ISR exception / parent SIGCHLD on exit
       → send_signal(target, sig)
       → signal_pending |= mask

  blocked syscall: signal_pending() → return -ERESTARTSYS / -EINTR

  syscall exit (exit_to_user_mode → do_signal)
       ├─ SIGKILL / uncaught default-terminate → signal_exit_current
       ├─ SIGSTOP → sleep until SIGCONT
       ├─ caught: -ERESTARTSYS restarts under SA_RESTART, else EINTR;
       │          sigframe on the user stack, syscall frame enters the handler
       └─ none:   -ERESTART* restarts the syscall
```

ASCII:

```text
  source ──► pending mask ──► do_signal (syscall exit)
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
                 exit            block          user handler
                                                    │
                                                    ▼
                                              rt_sigreturn
```

## 4. Responsibilities

- ISR maps CPU exceptions to signals for user frames (`isr_handlers.c`).
- Page fault path may `send_signal(SIGSEGV)` then **synchronous** `process_exit` in `fault.c`.
- Spawn resets handlers to `SIG_DFL`; fork copies signal state via `memcpy`.

## 5. Subsystem boundaries

- `_NSIG = 32`; API `sigset_t` is 64-bit — size mismatch debt.
- `sys_sigaction` is not full Linux `rt_sigaction` (no sigsetsize arg).
- Exec does **not** reset signal state (non-POSIX).

## 6. Relations to other subsystems

| Neighbor | Interaction |
|----------|---------------|
| Scheduler | never delivers; `schedule()` only sleeps and wakes |
| Process | exit sends SIGCHLD to parent |
| Syscalls | sigreturn restores `task_t` from saved frame |
| Interrupts | exception → signal mapping |

## 7. Visual maps

```text
  pending:  [ bit sig ... ]
  mask:     blocks delivery in handler loop
  ignored:  SIG_IGN skips handler

  delivery order (hard-coded):
    KILL > STOP > fatal faults > term group > CONT > CHLD > custom handlers
```

## 8. Important invariants

1. SIGKILL/SIGSTOP cannot be caught or ignored.
2. `sys_kill`: `pid > 0`, process groups (`0`, `< -1`) and `kill(-1)`, which
   spares init, the caller and kernel tasks (Linux `kill_something_info()`).
   SIGKILL/SIGSTOP never enter a mask: every user-supplied mask goes through
   `set_current_blocked()`.
3. User handler needs valid user stack in [0x400000, 0x7FFFFFFFFFFF], 16-byte aligned.
   Frames are placed by `signal_pick_handler_sp()` (also used from
   `signals_deliver_from_irq_frame`) with `SIGNAL_HANDLER_TOP_MARGIN` below
   `USER_STACK_TOP` so delivery does not land in the canary / overrun page.
4. `sa_flags` stores `SA_RESTART` / `SA_SIGINFO` / `SA_RESTORER` on `rt_sigaction`.
   `-ERESTARTSYS` (read, write, wait4, futex) restarts under `SA_RESTART`;
   `pause` / `rt_sigsuspend` return `-ERESTARTNOHAND` (EINTR when a handler
   runs); `nanosleep`, `poll` and `select` return `EINTR` (Linux).
5. `act->sa_mask` overwrites entire process mask on sigaction.
6. Linux/musl `sigset_t` places signal N in bit (N-1). The syscall ABI
   converts with `linux_sigword_to_ir0` / `ir0_sigword_to_linux` so
   `sigprocmask`/`pselect6` match `SIGNAL_MASK(N)`.

## 9. Debugging tips

- musl `SA_RESTORER` is stored; restorer trampoline is not a full musl port.
- Asynchronous signals are delivered at the syscall exit, on
  rt_sigreturn's way out (`do_signal(p, restored_ctx)`) and on the
  interrupt return to userspace (`irqentry_exit_to_user_mode()`,
  `kernel/entry.c`), so a task spinning without syscalls still gets them.
- An uncaught signal takes its full signal(7) default: Ign drops it, Stop
  (STOP, TSTP, TTIN, TTOU) sleeps until SIGCONT, everything else ends the
  task.
- Diagnostics go through `signal_trace()` (`kernel/signal_diag.c`);
  `kernel/signals.c` has no inline klog/KTM.
- `rt_sigreturn` copies the frame from the user stack pointer only.

## 10. Future roadmap

- Socket paths that test `signal_pending()` before sleeping still return
  `-EINTR`; the shared polling sleep returns `-ERESTARTSYS`.
- Process groups, `kill(0)`, permission checks on send.
- Reset signal state on exec (POSIX).
- `pause`, `sigsuspend`, `signalfd`, `inotify`.
- Real-time queued signals.

See: `IR0-process`, `IR0-interrupts`, `IR0-scheduler`.
