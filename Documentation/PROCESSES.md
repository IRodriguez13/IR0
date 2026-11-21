# IR0 Process Model

> **Last verified:** 2026-11-20
> **Source of truth:** `kernel/process/exit.c`, `kernel/process/wait.c`,
> `kernel/syscalls/process_syscalls.c`, `kernel/signals/signals.c`,
> `kernel/signals/syscalls.c`, `kernel/process/domains.c`,
> `includes/ir0/files_struct.h`, `includes/ir0/fd_types.h`,
> [`uaccess.md`](uaccess.md),
> [`releases/PROCESS_LIFECYCLE_SPEC.md`](releases/PROCESS_LIFECYCLE_SPEC.md)

IR0 process handling centers on practical lifecycle management plus incremental
Unix credential semantics.

## Core Areas

- Lifecycle split under `kernel/process/` (`exit.c`, `wait.c`, fork/create, …).
- Syscall integration in `kernel/syscalls/process_syscalls.c` (and related).
- Scheduler handoff through scheduler API.
- Signal and wait/reap paths integrated with process state transitions.

## Process Data Highlights

- PID/PPID and process list linkage.
- Address-space and task/context metadata.
- File descriptor table and working directory. The table starts small and
  `files_grow()` doubles it up to `RLIMIT_NOFILE`, which itself cannot exceed
  `IR0_NOFILE_CEILING` (4096). The soft default is `IR0_NOFILE_SOFT_DEFAULT`
  (1024). One allocator installs a descriptor: `fd_alloc_lowest()`.
- Credentials: `uid/gid/euid/egid` and `umask`.
- Pending signal state and termination metadata. The handler table is a
  `sighand_struct`. `clone(CLONE_SIGHAND)` shares it; without the flag the
  child gets a copy. Linux rejects `CLONE_SIGHAND` without `CLONE_VM`
  (`-EINVAL` in `sys_clone`). Pending signals and the blocked mask stay
  per task. `SIG_IGN` updates the ignored bit on every task that holds the
  table (`process_sighand_note_ignore`). `exec` calls
  `process_signals_unshare()` and copies the table when more than one task
  still uses it.
- The interrupted user context is not kept in the kernel. Delivery writes a
  `struct sigframe` on the user stack, and that frame carries the Linux
  `ucontext`. The handler edits the `ucontext`; `rt_sigreturn(2)` imports it
  back onto the private `sigcontext`. On arm64 the `mcontext` is the
  `sigcontext` itself (one copy). On x86-64 the private `sigcontext` and the
  936-byte Linux `ucontext` are separate.

## Signal return context

`handle_signal()` / `signals_deliver_from_irq_frame()`
(`kernel/signals/signals.c`) build the handler frame on the user stack;
`sys_sigreturn()` (`kernel/signals/syscalls.c`) copies it back and
validates it, and a frame that cannot be read or does not validate ends in
SIGSEGV. Handlers nest as in Linux: a signal that is not blocked while a
handler runs gets its own frame below the running handler's.

`signal_enter_pending` (separate field) gates syscall exit into the handler frame; see `kernel/entry/syscall_dispatch.c` and `kernel/lib/signals.c`.

Signal frame / `siginfo` setup **must** use `copy_to_user_mm`
(never CR3 + `memcpy` to the user stack). Contract and fault tags:
[`uaccess.md`](uaccess.md).

## Exit, reparent, wait

On `process_exit()` (`kernel/process/exit.c`):

1. Reap the dying task’s own zombie children (`process_reap_zombies`).
2. **`process_reparent_children`** — every live child with `ppid == dying->pid`
   gets **`ppid = 1`** (init), Linux/Unix orphan adoption.
3. Mark the dying task `PROCESS_ZOMBIE` until its parent (or init) `wait`s it.
4. Notify parent (`SIGCHLD` / wait wake) then schedule away.

Edge case: if PID 1 is missing or the dying task *is* init, orphans are detached
with **`ppid = 0`** (no alternate subreaper). ISD/runit does not implement
userspace adoption; it relies on this kernel path.

## Current Credential Semantics

- Effective credentials are used in permission-critical checks.
- Identity syscall surface includes:
  - `getuid/geteuid/getgid/getegid`
  - `setuid/setgid`
  - `umask`
- Minimal user model exists for root/user separation workflows.

## Strengths

- Clear process lifecycle with explicit wait/reap handling.
- Orphan reparent to init matches classical Unix expectations.
- Credential fields are now actively used in policy decisions.
- Better alignment with Unix-like ownership and permission flow.

## Weak Points

- Full account/session model remains intentionally lightweight.
- Some advanced fork/exec/credential edge cases are still maturing.
- Thread-level model is not a primary focus yet.
- PID 1 must `wait` reparented zombies; runit is not a full systemd-style reaper.
