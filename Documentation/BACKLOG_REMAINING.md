# IR0 — Post-0.0.1 backlog (honest remaining work)

> **Last verified:** 2026-07-10  
> **Source of truth:** `Documentation/ROADMAP.md`, code under `fs/`, `drivers/storage/`,  
> `scripts/linux_abi/`, Makefile gates. Prefer this file for **what is still open**;  
> ROADMAP holds history and tier %.

## Closed (do not re-open without regression)

| Item | Evidence |
|------|----------|
| Linux ABI cola 0.0.1 | `IR0_0.0.1_ABI_BOARD.md` |
| `/proc`/`/sys` → `fd_table` + dup | `linux-abi-audit-dup` |
| `/heart` MVP | `smoke-heart` |
| ARCH-3 virtual fds / `FD_SYS`/`FD_DEV` | `agent-fast` + tier1 smokes |
| Process ownership split + backend facades | `kernel-x64.bin` + `arch-guard` |
| FAT16 RO + QEMU smoke | `smoke-fat16-mount` |
| Host `ir0_block_*` contracts | `tests/host/test_blockdev_facade.c` |
| System power (sync + reboot/halt/poweroff + runit) | `smoke-runit-power`, `smoke-runit-busybox-halt`; `vfs_sync`/`sys_sync`; stage3 stub; BusyBox applets |
| GPT partition smoke | `smoke-gpt-partition` (`GPT_PARTITION_OK`) |
| EXT2 read-only mount/read | `smoke-ext2-mount` (`EXT2OK`) |
| AHCI PCI detect | `smoke-ahci-detect` (`AHCI_DETECT_OK`) |
| `prlimit64` / `getrlimit` real limits | `smoke-posix-depth` |
| `epoll` + `pselect6` (poll-backed MVP) | `smoke-posix-depth` |
| PTY pair + `TIOCGWINSZ` / `TIOCGPTN` | `smoke-posix-depth` (`/dev/ptmx`, `/dev/pts/0`) |
| musl `CLONE_THREAD` + pthread smoke | `smoke-musl-pthread` (`MUSL_PTHREAD_OK`) |
| Multi-arch platform split (CPUID/power) | `arch/*/sources/platform.c` + `ir0/platform_ops.h`; `arch-guard` |

## Open — storage / drivers

| # | Item | Status | Next proof |
|---|------|--------|------------|
| — | FAT16 **write** | **Blocked** | FAT-backed `vfs-write` audit PASS first |
| — | AHCI full `ir0_block_*` I/O | Open | read smoke beyond PCI class detect |

## Open — T1 POSIX

| Item | Status | Next proof |
|------|--------|------------|
| Full musl `pthread_create` (robust list / futex edge) | Open | libc pthread beyond raw `clone` smoke |
| PTY SIGWINCH / multi-pty | Future | beyond single global pair |

## Open — multi-arch consolidation

| Item | Status |
|------|--------|
| Generic TLS/cache facade beyond x86 FS base | Open |
| VM policy vs PTE walker per arch | Open (W5b) |
| Trap/context adapters | Open |
| ARM64 boot functional | Future (explicit oleada) |

## Future / P2 (explicit non-goals for current merges)

- CAD real, `RESTART2` string, kexec, software suspend
- QEMU `isa-debug-exit` clean process exit
- `platform_ops` RPi vs QEMU virt (beyond x86 ACPI/KBC MVP)
- Extra `/proc`/`/sys` nodes (incremental)
- Expand `/heart/src` file list
- TCP / `AF_UNIX`, Rust/C++ drivers, SMP/CFS, T3 WM

## Explicit non-goals until gates green

- FAT16 rw without FAT-backed vfs-write audit PASS
- Declaring TCC / Doom “stable” without their smokes green
- T3 compositor inside the kernel tree
- ARM64 boot as part of multi-arch consolidation merge
