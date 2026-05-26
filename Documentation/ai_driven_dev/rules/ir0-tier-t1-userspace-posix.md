<!-- IR0 AI dev rule: ir0-tier-t1-userspace-posix -->
<!-- alwaysApply: false -->
<!-- description: T1 (~40%) — Userspace POSIX mínimo init+musl fork/exec ABI -->

# T1 — Userspace POSIX Mínimo (init + musl)

## Goal

Boot real `/sbin/init` with `KERNEL_DEBUG_SHELL=0`, static musl binary, stable syscall subset for shell + core utilities.

## Mandatory web research

Before adding syscalls or changing register/stack layout:

1. **musl** x86-64 syscall convention and which NR musl expects at startup.
2. **Linux x86-64 ABI** — `%rdi,%rsi,%rdx,%r10,%r8,%r9`, `%rax` syscall nr, `int 0x80` vs `syscall` (match `arch/` stub).
3. **ELF psABI** — `AT_*` aux vector if musl needs it (often gap vs current loader).

Sources: [syscall64](https://filippo.io/linux-syscall64/), musl `src/env/__libc_start_main.c`, System V AMD64 ABI.

## Verify in tree first

- `setup/pid1/` — external init binary; use `make load-init` workflow.
- `kexecve` / `elf_setup_stack` — argc/argv/envp layout.
- `fork()` COW and fd inheritance — grep `process.c` before assuming POSIX completeness.

## Multi-agent split

| Agent | Task |
|-------|------|
| Research | musl minimal syscall gap list vs `syscall_table_rw` |
| Implement | Missing NR + fix exec/fork/wait edge cases |
| Integrate | Cross-compile busybox/musl init, document in TOOLING (English) |

## Syscall priority (typical musl blockers)

`execve`, `fork`, `wait4`, `brk`, `mmap`/`munmap`, `openat`, `rt_sigaction`, `arch_prctl` (if TLS), `set_tid_address`, `exit_group`.

## Done criteria

- QEMU boot loads `/sbin/init` without falling back to debug shell.
- Static musl `hello` or busybox `sh` runs one command via fork+exec.
- Syscall additions wired in Kconfig/Makefile only if needed; no stub that returns fake success.
