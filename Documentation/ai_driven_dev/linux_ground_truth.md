# Linux ground truth (musl / BusyBox compatibility)

> **Last verified:** 2026-06-26  
> **Source of truth:** same ELF as IR0 workloads, Linux `strace`/`gdb`/`/proc/self/maps`,
> IR0 serial probes (`[LINUX_ABI_AUDIT]`, `[D1.12]`, `[D1.14]`), `includes/ir0/abi/*`,
> `scripts/linux_abi/contracts.json`

## Mandatory workflow (all important ABI work)

Before changing **syscall**, **MM (mmap/brk)**, **signals**, **TTY**, **pipes**, **wait**,
or **exec/fork** semantics for musl/BusyBox/TCC:

1. Run the **same binary** on **Linux** (BusyBox/musl or dedicated probe ELF).
2. Capture ground truth: `strace`, `gdb` (if needed), `/proc/self/maps`, `perf`/`ftrace` when useful.
3. Run the **same workload** on **IR0** (QEMU + injected init or tier smoke).
4. Find the **first observable divergence** (syscall order, errno, return value, VMA, signal).
5. Fix IR0 minimally; do not invent behaviour without Linux evidence.
6. Lock with **host test + ktest + smoke** and register evidence in the ABI table below.

## Automated audit

```bash
# Full audit for all enabled contracts (brk + wait4 + read + mmap + mount)
make linux-abi-audit

# Single contract iteration
make linux-abi-audit-wait4
make linux-abi-audit-read
make linux-abi-audit-mmap
make linux-abi-audit-mount

# Report artifacts
build/linux_abi_audit/report.md
build/linux_abi_audit/report.json
build/linux_abi_audit/linux/brk/   # strace + stdout
build/linux_abi_audit/ir0/brk/   # QEMU serial

# Faster iteration (skip slow gates; not for release)
LINUX_ABI_SKIP_KTEST=1 LINUX_ABI_SKIP_HOST=1 python3 scripts/linux_abi_audit.py --contract brk
```

Registry: `scripts/linux_abi/contracts.json` — set `"enabled": true` when a contract auditor lands.

## ABI contract table

Status values: **UNKNOWN** | **PARTIAL** | **LINUX-LIKE** | **VERIFIED**

Evidence columns: **host trace** | **IR0 trace** | **host tests** | **ktests** | **smoke**

| Syscall / area | Status | host trace | IR0 trace | host tests | ktests | smoke | Notes |
|----------------|--------|:----------:|:---------:|:----------:|:------:|:-----:|-------|
| execve | PARTIAL | — | — | — | partial | tier1 | ELF load path; no paired audit yet |
| brk | VERIFIED | ✓ | ✓ | ✓ | ✓ | partial | `make linux-abi-audit`; probe `scripts/linux_abi/workloads/brk_probe.c` |
| mmap | VERIFIED | ✓ | ✓ | ✓ | ✓ | partial | `make linux-abi-audit-mmap`; anon RW/PROT_NONE/MAP_FIXED/munmap/bad-fd errno; **no file-backed mmap** |
| munmap | PARTIAL | — | — | — | partial | — | No paired audit |
| mprotect | UNKNOWN | — | — | — | — | — | |
| fork | PARTIAL | — | — | — | partial | — | |
| clone | UNKNOWN | — | — | — | — | — | pthread smoke separate |
| wait4 | VERIFIED | ✓ | ✓ | — | ✓ | — | `make linux-abi-audit-wait4`; probe `scripts/linux_abi/workloads/wait4_probe.c`; ktest `wait4_status` |
| exit | PARTIAL | — | — | — | partial | — | |
| signals | PARTIAL | — | — | ✓ | partial | — | `test_signal_rt_sigaction_abi` |
| pipe | LINUX-LIKE | — | — | ✓ | ✓ | — | host pipe tests |
| dup | PARTIAL | — | — | — | partial | — | |
| poll | PARTIAL | — | — | ✓ | partial | — | KTM poll matrix host test |
| select | UNKNOWN | — | — | — | — | — | |
| ioctl | PARTIAL | — | — | — | partial | — | TTY/winsize legacy captures |
| read | VERIFIED | ✓ | ✓ | — | ✓ | — | `make linux-abi-audit-read`; probe `read_probe.c` (pipe/EOF/EBADF); ktest `syscall_pipe` |
| write | LINUX-LIKE | — | — | — | partial | ✓ | |
| lseek | PARTIAL | — | — | — | partial | — | |
| openat | PARTIAL | — | — | ✓ | partial | — | open flags host/VFS tests |
| rename | PARTIAL | — | — | — | partial | — | |
| unlink | PARTIAL | — | — | — | partial | — | |
| stat | PARTIAL | — | — | ✓ | partial | — | `test_stat_user_abi` |
| mount | VERIFIED | ✓ | ✓ | — | ✓ | partial | `make linux-abi-audit-mount`; tmpfs none `/tmp/ir0mnt`, rw/umount, ENOENT/ENODEV; **FAT16 smoke separado** |
| umount2 | PARTIAL | — | — | — | partial | — | |

Update **brk** row after each green `make linux-abi-audit`. Enable the next contract in
`contracts.json` only when workload + compare scripts exist.

## Suggested next contracts (priority)

1. **pipe** — `pipe`+`poll` minimal probe (ordering + `EAGAIN`).
2. **read TTY** (optional) — canon read `echo hi\n` → ret=8 vs D1.13 capture.
3. **mmap file-backed** — when implemented; audit today covers anon only.
4. **umount2** — flags edge cases vs Linux.

## Legacy reference captures (D1.13 TTY)

```bash
# Linux PTY + strace + gdb memmove (BusyBox ash, echo hi)
python3 scripts/d1_13_linux_ground_truth.py

# IR0 smoke (headless ash interactive)
make smoke-runit-ash-interactive
grep -E 'D1\.14|PF_AUDIT|SYS_READ|ASH_COMMAND' /tmp/runit-ash-smoke.log
```

| Metric | Linux (PTY) | IR0 (before D1.15) |
|--------|-------------|---------------------|
| `read(0)` after `echo hi\n` | 8 | stall (no return) |
| memmove max `n` @ `0x4422bf` | `0x1e` | ~`0x1.7 MiB` |
| mmap ↔ stack gap | ~100+ KiB–MiB | 4 KiB guard only |

## Related

- Orchestrator: `scripts/linux_abi_audit.py`
- Compare/parser: `scripts/linux_abi/compare.py`, `parse_trace.py`
- mmap gap policy: `USER_STACK_MMAP_GAP` in `config.h` / `includes/ir0/abi/mmap_contract.h`
- brk policy: `includes/ir0/abi/brk_contract.h`
- Forensics: `CONFIG_KTM_MALLOC_FORENSICS`, `ktm/d1_13_malloc_pf_diag.c` (`[D1.14]` tags)
