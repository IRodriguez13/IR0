# IR0 — Post-0.0.1 backlog (honest remaining work)

> **Last verified:** 2026-07-11  
> **Source of truth:** `Documentation/ROADMAP.md`, code under `fs/`, `drivers/storage/`,  
> `scripts/linux_abi/`, Makefile gates. Prefer this file for **what is still open**;  
> ROADMAP holds history and tier %.

## Closed (do not re-open without regression)

| Item | Evidence |
|------|----------|
| Linux ABI cola 0.0.1 | `IR0_0.0.1_ABI_BOARD.md` |
| `/proc`/`/sys` → `fd_table` + dup | `linux-abi-audit-dup` |
| `/heart` MVP + src expand | `IR0_LEGACY_SMOKE=1 smoke-heart` (`HEART_SRC_EXPAND_OK`, cmdline/osrelease) |
| `/proc/cmdline` + `/sys/kernel/osrelease` | `smoke-heart` mirrors |
| ARCH-3 virtual fds / `FD_SYS`/`FD_DEV` | `agent-fast` + tier1 smokes |
| Process ownership split + backend facades | `kernel-x64.bin` + `arch-guard` |
| FAT16 RO + QEMU smoke | `smoke-fat16-mount` |
| Host `ir0_block_*` contracts | `tests/host/test_blockdev_facade.c` |
| System power (sync + reboot/halt/poweroff + runit) | `smoke-runit-power`, `smoke-runit-busybox-halt`, `smoke-runit-busybox-poweroff`, `smoke-runit-busybox-reboot` |
| ACPI PM1a QEMU poweroff + kexec/suspend ABI stubs | FADT via on-demand map + `ACPI_PM1A_POWEROFF`; stubs `smoke-reboot-kexec-enosys`, `smoke-reboot-suspend-stub` |
| POSIX-2 setsid/setpgid + SIGHUP/TTY | `smoke-posix-setsid`, `smoke-posix-sighup-tty` |
| Real fork COW A–F (HOST) | `make smoke-mm-cow-lazy` (FASE40 A–F) — verified 2026-07-11 |
| GPT / EXT2 RO / AHCI detect+read + multi | `smoke-gpt-partition`, `smoke-ext2-mount`, `smoke-ahci-read`, `smoke-ahci-multi` |
| FAT16 write + vfs-write audit | `linux-abi-audit-vfs-write-fat` VERIFIED |
| PTY multi + `TIOCSWINSZ` | `smoke-pty-winsz` |
| musl pthread libc | `smoke-musl-pthread-libc` |
| TLS facade (`arch_set_tls`) | `arch_portable.h`; W10b PTE deferred |
| ARM64 boot stub + `kernel-arm64.bin` link | `smoke-arm64-boot`; `make ARCH=arm64 kernel-arm64.bin` |
| AF_UNIX + TCP loopback + `send`/`recv` | `smoke-stream-sock` (`STREAM_SENDRECV_OK`) |
| `isa-debug-exit` + CAD/RESTART2 tags | `smoke-isa-debug-exit` |
| ARM64 `platform_ops` virt + RPi stub | `arch/arm64/sources/platform.c` |
| KTM boot suite | `make ktm-run` (pass=16 incl. `process.reclaim_exit`) |
| KTM userdev | `ktm-userdev-run`, `ktm-userdev-cow-run` |
| Kernel `[FASE` serial retired | arch-guard `ktm-no-fase` |
| PERF-1 `sys_gettid` | no per-call GETTID spam |
| FASE→KTM Open residual | 41 reclaim MVP + COW A–F verified; 52/55 = HOST only |

## Open

_(vacío — solo Future abajo)_

## Future / P2 (simple → complejo; una oleada a la vez)

| # | Item | Next proof |
|---|------|------------|
| F1 | ~~ACPI FADT map seguro~~ | **DONE** 2026-07-11 — `ACPI_FADT_MAPPED` + `ACPI_PM1A_POWEROFF` |
| F2 | AHCI NCQ | smoke AHCI read/multi + NCQ tag |
| F3 | AML `_S5` / soft-off beyond raw PM1a | poweroff without relying on port guess alone |
| F4 | kexec mínimo | beyond `-ENOSYS` stub |
| F5 | Suspend S3 more real | beyond ENOSYS stub |
| F6 | NVMe MVP | detect+read smoke |
| F7 | ARM64 MM / userspace | beyond freestanding `kernel-arm64.bin` |
| F8 | TCP Internet / real NIC | beyond loopback |
| F9 | SMP / CFS | sched oleada |
| F10 | Rust/C++ driver ABI | DRV-* |
| F11 | T3 WM | **userspace only** |
| F12 | TCC/Doom “stable” | STABLE.md HOST |

## T3 prep checklist (no WM in kernel)

| Prerequisite | Status | Evidence |
|--------------|--------|----------|
| Stream sockets (local) | OK | `smoke-stream-sock` |
| Input events | OK | T2 path |
| Framebuffer | OK | T2 path |
| Kernel-side WM | **Out of scope** | T3 rule |
