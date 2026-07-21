# Process lifecycle capability — ground-truth specification

> **Last verified:** 2026-06-27  
> **Source of truth:** `scripts/linux_abi/workloads/process_lifecycle_probe.c`,  
> `scripts/linux_abi/compare.py` (`compare_process_lifecycle`),  
> `scripts/linux_abi/contracts.json` (`process_lifecycle`)

## Scope

End-to-end **Process lifecycle** certification for IR0 0.0.1 base (0.1 foundation).
Maintainer model: one capability bundle, not isolated syscall contracts.

### In scope (required for VERIFIED)

| # | Scenario | Audit op(s) |
|---|----------|-------------|
| 1 | `fork` → child `_exit(42)` → `wait4(pid)` | `wait4_exit42` |
| 2 | `fork` → `_exit(10)` → `wait4(-1)` | `wait4_any` |
| 3 | `wait4(WNOHANG)` while child alive → `0`; then reap | `wait4_wnohang_alive`, `wait4_wnohang_reap` |
| 4 | `wait4(-1)` with no children → `-ECHILD` | `wait4_echild` |
| 5 | `fork` + `execve(helper)` → status `0` | `execve_ok` |
| 6 | `execve` ENOENT in child → exit `127` convention | `execve_noent` |
| 7 | `_exit(1)` → wait status `0x0100` | `wait4_exit1` |
| 8 | `kill(child, SIGTERM)` → `WIFSIGNALED` / signal 15 | `kill_sigterm` |
| 9 | SIGCHLD default + wait (no userspace handler) | `sigchld_default_wait` |
| 10 | Reparent orphan via init (PID 1 cooperative) | `reparent_mid_wait`, `reparent_orphan_wait` |

### Out of scope (0.1 — Terminal / job control)

- `setpgid`, `getpgid`, `getpgrp`, `setsid`, controlling TTY, job control.

### Optional (not required for initial VERIFIED)

- SIGCHLD userspace handler (`rt_sigaction`)
- `SIGUSR1` ignored + wait
- `clone` / `CLONE_THREAD`

## Audit contract

- **Probe:** static musl ELF `process_lifecycle_probe.c` (+ `exec_helper.c` on disk).
- **Tag:** `[PROC_LIFECYCLEOK]` on success.
- **Serial format:**  
  `[LINUX_ABI_AUDIT][process_lifecycle] step=N op=NAME ret=R errno=E [status=0xS] [flags=...]`
- **Linux ground truth:** `strace -f -e trace=fork,wait4,waitpid,execve,exit,exit_group,kill`
- **IR0 workload:** QEMU userspace ISO, probe injected as `/sbin/init` (step 10).

## Step 10 — reparenting

When `getpid() != 1` (Linux host user run), emit `reparent_skip` with `flags=not_pid1`.
Compare treats Linux skip as acceptable; **IR0 must pass** `reparent_mid_wait` and
`reparent_orphan_wait` when running as PID 1.

## Divergence taxonomy

| Class | Action |
|-------|--------|
| Bug real | Fix kernel (`kernel/process.c`, signals, elf_loader) |
| Linux acceptable | Document in capability board |
| Feature missing | Defer to 0.1 sub-capability |
| Instrumentation | Fix probe / parser |
| Harness | Fix run scripts / QEMU |

## Gates (capability VERIFIED)

```bash
make linux-abi-audit-process-lifecycle
make kernel-tests          # wait4_*, signal ktests
make smoke-tier1
make smoke-mm-cow-lazy
IR0_INCLUDE_QA=1 make release-0.0.1
```

## References

- Linux [wait4(2)](https://man7.org/linux/man-pages/man2/wait4.2.html), [wait(2)](https://man7.org/linux/man-pages/man2/wait.2.html)
- musl x86-64 syscall ABI
