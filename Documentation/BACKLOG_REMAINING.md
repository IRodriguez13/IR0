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
| ARCH-3 residual: drop `FD_SYS`/`FD_DEV` virtual I/O | `agent-fast` + `smoke-tier1` + `smoke-heart` + `smoke-fat16-mount` + `linux-abi-audit-dup` |
| ARCH process split: `kernel/process/*.c` by ownership | `kernel-x64.bin` + gates below |
| Seal console/video/input/audio/partition facades | `arch-guard` OK; impl in `includes/ir0/*_backend.c` |
| Syscall anti-fragmentation policy | `IR0_SYSCALL_PSEUDOFS_MAP.md` |
| FAT16 read-only QEMU smoke | `smoke-fat16-mount` |
| System power MVP (`sys_reboot` + `kernel/power/power_manag`) | `agent-fast` + `smoke-runit-power` |

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
| — | FAT16 **write** | **Blocked** (gap known) | First divergence: all mutators → `-EROFS` (`fat16_rofs()` in `fs/fat16_disk.c`). `linux-abi-audit-vfs-write` today is MINIX-only (`run_ir0_vfs_write.sh`). Need FAT-backed runner + PASS before any write impl |

## P1 — T1 POSIX depth

| Item | Status |
|------|--------|
| `epoll` / `pselect6` | Not started |
| `prlimit` / `getrlimit` | Not started |
| Futex robustness / musl pthread join | **Blocked** — `smoke-musl-pthread` boots then stalls (no `MUSL_PTHREAD_OK`); harness profile `musl-pthread` added in `smoke_autokill.py`; same hang on `dae46ae` baseline |
| PTY + `TIOCGWINSZ` / `SIGWINCH` | Not started |

## P1 — system power residual

| Item | Status |
|------|--------|
| `sys_reboot` + `kernel_system_shutdown` + `arch_system_*` | **Done** (MVP) — `kernel/power/`, `__NR_reboot` 169 |
| Best-effort `ir0_block_flush_all` + `ir0_driver_shutdown_all` | **Done** |
| Full `vfs_sync` / ordered umount before poweroff | Not started |
| runit stage 3 ordered stop → then reboot | Not started (smoke calls `reboot(2)` from a supervised service) |
| BusyBox `reboot`/`halt`/`poweroff` applets | Off in `fase58_busybox_defconfig` |
| CAD real / `RESTART2` string / kexec / suspend | Not started |
| QEMU clean exit (`isa-debug-exit`) smoke | Not started (`-no-reboot`; PASS = serial tags) |
| `platform_ops` (QEMU vs PC vs RPi) separate from arch | Not started |

## P1 — multi-arch consolidation (before second arch is “easy”)

| Item | Status |
|------|--------|
| Split `arch_interface.c` `#if` matrix → `arch/{x86-64,arm64,riscv64}/platform.c` | Not started |
| Generic TLS / cache APIs vs x86 FS base / CPUID leafs | Not started |
| VM policy (`map`/`unmap`/`protect`) vs PTE walker per arch | Not started |
| Common trap/context adapters | Not started |
| `platform_ops` vs `arch_ops` (RPi ≠ ARM64 virt) | Not started |

ARM64 remains embryonic; first second architecture is the real dependency-inversion test. RISC-V after ARM64 should be cheaper.

## P1 residual — pseudo-fs / ABI

| Item | Status |
|------|--------|
| Drop remaining `FD_SYS_BASE` / `FD_DEV_BASE` virtual I/O | **Done** — I/O solo `fd_table` (`is_devfs` / `is_pseudo`); macros `FD_*_BASE` retirados de `syscalls_glue.h` |
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
