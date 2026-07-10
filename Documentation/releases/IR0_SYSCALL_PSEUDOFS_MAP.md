# IR0 — Syscall modules and pseudo-fs debt map

> **Last verified:** 2026-07-10  
> **Source of truth:** `kernel/syscalls/`, `fs/pseudo_fs_registry.c`, `includes/ir0/pseudo_fs.h`

## Syscall layout (domain specialization)

| Module | Role | Anti-pattern |
|--------|------|--------------|
| `kernel/syscalls.c` | stdin glue only | Re-growing into a monolith |
| `syscall_dispatch.c` | Linux ABI table | Per-syscall `.c` files |
| `fs_syscalls.c` | FS I/O + open routing | Copy-pasted `FD_*_BASE` branches |
| `fs_path_syscalls.c` | chdir/getcwd/… | — |
| `io_syscalls.c` | pipe/poll/dup/fcntl | — |
| `process_syscalls.c` | fork/wait/exec | — |
| `mm_syscalls.c` | brk/mmap | — |
| `socket_syscalls.c` / `time_syscalls.c` | net / time | — |

Policy: `~/.cursor/rules/kernel-linux/ir0-syscall-anti-fragmentation.mdc`.

## Pseudo-fs architecture

```text
open/stat path
  → ir0_stat_path_routed / sys_open_routed_resolved
      → /proc|/sys|/heart  → pseudo_fs_registry (ops/ctx)
      → /dev               → devfs
      → else               → vfs backends (MINIX/tmpfs/…)
```

`/heart` is an **extra** mount prefix that reexports `/proc` and `/sys` nodes
plus IR0 metadata; it does **not** replace them.

## Debt inventory

| Pri | Item | Status |
|-----|------|--------|
| P0 | Virtual fds (`PSEUDO_FS_*_FD_BASE` / `FD_PROC_BASE`) break dup/fcntl | **Done** — registry + pid dirs/files en `fd_table` |
| P1 | Duplicated virtual-fd branches in read/write/close/lseek | **Done** (proc); residual `FD_SYS`/`FD_DEV` legacy |
| P1 | `PSEUDO_FS_MAX_ENTRIES` | Raised to 96 (heart table) |
| P2 | Full Linux proc/sys surface | Incremental `pseudo_fs_register` |
| P2 | `/heart` facade | **Done** — `/heart/{README,proc,sys,kernel,src}` |
| P2 | `/heart/src` kernel sources | **Done** — build-time blob (`scripts/gen_heart_src_blob.py`) |

## Related gates

- `make agent-fast`, `make smoke-tier1`
- `make diag-contract CONTRACT=dup` (prefer `/proc/uptime` after fd unify)
- `make smoke-heart` (`HEART_*` tags)
