# IR0 Process Model

IR0 process handling centers on practical lifecycle management plus incremental
Unix credential semantics.

## Core Areas

- Lifecycle and tables in `kernel/process.c` and `kernel/process.h`.
- Syscall integration in `kernel/syscalls.c`.
- Scheduler handoff through scheduler API.
- Signal and wait/reap paths integrated with process state transitions.

## Process Data Highlights

- PID/PPID and process list linkage.
- Address-space and task/context metadata.
- File descriptor table and working directory.
- Credentials: `uid/gid/euid/egid` and `umask`.
- Pending signal state and termination metadata.

## Current Credential Semantics

- Effective credentials are used in permission-critical checks.
- Identity syscall surface includes:
  - `getuid/geteuid/getgid/getegid`
  - `setuid/setgid`
  - `umask`
- Minimal user model exists for root/user separation workflows.

## Strengths

- Clear process lifecycle with explicit wait/reap handling.
- Credential fields are now actively used in policy decisions.
- Better alignment with Unix-like ownership and permission flow.

## Weak Points

- Full account/session model remains intentionally lightweight.
- Some advanced fork/exec/credential edge cases are still maturing.
- Thread-level model is not a primary focus yet.

