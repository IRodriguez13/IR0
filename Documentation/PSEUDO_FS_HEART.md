# IR0 — Pseudo-FS, `/heart`, and fd_table lifecycle

> **Last verified:** 2026-07-10  
> **Source of truth:** `fs/pseudo_fs_registry.c`, `fs/heartfs.c`, `fs/procfs.c`,  
> `kernel/syscalls/fs_syscalls.c`, `Documentation/releases/IR0_SYSCALL_PSEUDOFS_MAP.md`,  
> `make IR0_LEGACY_SMOKE=1 smoke-heart`, `make smoke-tier1`

## Architecture

```text
open/stat/getdents
  → ir0_*_path_routed / sys_open_routed_resolved
      → /proc|/sys|/heart  → pseudo_fs_registry (+ heart facade)
      → /dev               → devfs
      → else               → VFS backends (MINIX/tmpfs/FAT16/…)
```

Registry opens install **real** `current_process->fd_table` slots (`is_pseudo` +
`pseudo_fd_bind_t`). Userspace never receives global virtual fds
(`1001`/`1150`/`PSEUDO_FS_*_FD_BASE`) for new `/proc`/`/sys`/`/heart` opens.

## `/heart` (IR0-only facade)

| Path | Behavior |
|------|----------|
| `/heart/README` | Static identity text |
| `/heart/proc/…` | Alias → same ops/ctx as `/proc/…` |
| `/heart/sys/…` | Alias → same ops/ctx as `/sys/…` |
| `/heart/kernel/{version,build,features}` | IR0 metadata |
| `/heart/src/…` | Build-time embedded sources (`scripts/gen_heart_src_blob.py`) |

`/heart` does **not** replace `/proc` or `/sys`. No `heart_syscalls.c`; no new
`FD_*_BASE` for heart.

**Gate:** `make IR0_LEGACY_SMOKE=1 smoke-heart` → tags `HEART_OK`, mirrors, `HEART_SRC_OK`, `HEART_DUP_OK`.

## ARCH-3 (2026-07-10) — legacy `/proc` virtual fds removed

| Before | After |
|--------|-------|
| `proc_open` → `1001`/`1010`/`1150`/`1151` | `pseudo_bind_file_fd` / `pseudo_bind_dir_fd` |
| `proc_getdents(1150)` | `proc_readdir(path)` via `ir0_getdents_path_routed` |
| I/O branches on `FD_PROC_BASE` | `is_pseudo` on `fd_table` |

Residual (documented debt): legacy `FD_SYS_BASE` / `FD_DEV_BASE` virtual ranges
for older `/sys`/`/dev` paths still present in some I/O helpers.

## Syscall module policy

Domain modules under `kernel/syscalls/` only (fs / process / mm / io / net / time /
dispatch). **Forbidden:** one `.c` per syscall. See
`~/.cursor/rules/kernel-linux/ir0-syscall-anti-fragmentation.mdc` and
[`IR0_SYSCALL_PSEUDOFS_MAP.md`](releases/IR0_SYSCALL_PSEUDOFS_MAP.md).

## Related gates

```bash
make agent-fast
make diag-contract CONTRACT=dup    # /proc/uptime
make IR0_LEGACY_SMOKE=1 smoke-heart
make smoke-tier1
```
