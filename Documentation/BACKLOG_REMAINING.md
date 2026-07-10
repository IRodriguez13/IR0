# IR0 — Post-0.0.1 backlog (honest remaining work)

> **Last verified:** 2026-07-10  
> **Source of truth:** `Documentation/ROADMAP.md`, code under `fs/`, `drivers/storage/`,  
> `scripts/linux_abi/`, Makefile gates. Prefer this file for **what is still open**;  
> ROADMAP holds history and tier %.

## Closed recently (do not re-open without regression)

| Item | Evidence |
|------|----------|
| Linux ABI cola 0.0.1 (15 + wait4_wnohang + vfs_write) | `IR0_0.0.1_ABI_BOARD.md` |
| `/proc`/`/sys` registry opens → `fd_table` + dup on `/proc/uptime` | `linux-abi-audit-dup` |
| `/heart` MVP + `/heart/src` blob | `smoke-heart` |
| ARCH-3: remove `/proc` virtual fds `1001`/`1150`… | board note 2026-07-10 |
| Syscall anti-fragmentation policy | `IR0_SYSCALL_PSEUDOFS_MAP.md` |
| FAT16 read-only QEMU smoke | `smoke-fat16-mount` |

## P1 — storage / drivers (current focus)

| # | Item | Status | Next proof |
|---|------|--------|------------|
| 6 | `roadmap-phase2-driver-expansion` | Wired | `runtime-net-check` + `runtime-mount-check` |
| 7 | FAT16 on `block_dev` RO MVP | Done | `fs/fat16_disk.c`, `smoke-fat16-mount` |
| 8 | FAT16 QEMU smoke | Done | same |
| 9 | EXT2 read-only | **Not started** | new `fs/ext2/` + vfs_ops |
| 10 | AHCI/SATA | **Not started** | `ir0/blockdev` backend |
| 11 | Host contracts for `ir0_block_*` | **Done** | `tests/host/test_blockdev_facade.c`; RO flag no longer blocks reads |
| 12 | GPT partition table | **Not started** | `drivers/disk/partition.c` |
| — | FAT16 **write** | **Blocked** | Re-run `linux-abi-audit-vfs-write` on FAT first |

## P1 — T1 POSIX depth

| Item | Status |
|------|--------|
| `epoll` / `pselect6` | Not started |
| `prlimit` / `getrlimit` | Not started |
| Futex robustness / musl pthread join | Partial (smoke clone path) |
| PTY + `TIOCGWINSZ` / `SIGWINCH` | Not started |

## P1 residual — pseudo-fs / ABI

| Item | Status |
|------|--------|
| Drop remaining `FD_SYS_BASE` / `FD_DEV_BASE` virtual I/O | Open |
| More Linux `/proc`/`/sys` nodes via `pseudo_fs_register` | Incremental |
| Expand `/heart/src` file list | Optional |

## P2 / P3 (after storage phase2 stable)

- TCP stream + `AF_UNIX`
- Rust/C++ driver platform + modules
- SMP / CFS
- T3 WM (out of kernel tree)

## Explicit non-goals until gates green

- FAT16 rw without FAT-backed vfs-write audit PASS
- Declaring TCC / Doom “stable” without their smokes green
- T3 compositor inside the kernel tree
