# IR0 Process Model

> **Last verified:** 2026-07-29
> **Source of truth:** `kernel/process/exit.c`, `kernel/process/wait.c`,
> `kernel/syscalls/process_syscalls.c`, [`releases/PROCESS_LIFECYCLE_SPEC.md`](releases/PROCESS_LIFECYCLE_SPEC.md)

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
- File descriptor table and working directory.
- Credentials: `uid/gid/euid/egid` and `umask`.
- Pending signal state and termination metadata.

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
