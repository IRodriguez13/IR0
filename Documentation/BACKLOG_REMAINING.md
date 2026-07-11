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
| `/heart` MVP + src expand | `IR0_LEGACY_SMOKE=1 smoke-heart` (`HEART_SRC_EXPAND_OK`, cmdline/osrelease) |
| `/proc/cmdline` + `/sys/kernel/osrelease` | `smoke-heart` mirrors |
| ARCH-3 virtual fds / `FD_SYS`/`FD_DEV` | `agent-fast` + tier1 smokes |
| Process ownership split + backend facades | `kernel-x64.bin` + `arch-guard` |
| FAT16 RO + QEMU smoke | `smoke-fat16-mount` |
| Host `ir0_block_*` contracts | `tests/host/test_blockdev_facade.c` |
| System power (sync + reboot/halt/poweroff + runit) | `smoke-runit-power`, `smoke-runit-busybox-halt`, `smoke-runit-busybox-poweroff`, `smoke-runit-busybox-reboot` |
| ACPI PM1a QEMU poweroff + kexec/suspend ABI stubs | FADT PM1a via `ir0_acpi_pm_*`; `smoke-reboot-kexec-enosys`, `smoke-reboot-suspend-stub`; poweroff still `smoke-runit-busybox-poweroff` |
| POSIX-2 setsid/setpgid MVP | `smoke-posix-setsid` (`SETSID_OK` / `SETPGID_OK`) |
| Real fork COW (share + WP break) | `62cc512` / `496b55d`; `make smoke-mm-cow-lazy` (FASE40 A–F) |
| GPT / EXT2 RO / AHCI detect+read | `smoke-gpt-partition`, `smoke-ext2-mount`, `smoke-ahci-read` |
| AHCI dual-port (`sda`+`sdb`) | `smoke-ahci-multi` (`AHCI_MULTI_OK`) |
| FAT16 write + vfs-write audit | `linux-abi-audit-vfs-write-fat` VERIFIED |
| PTY multi + `TIOCSWINSZ` | `smoke-pty-winsz` |
| musl pthread libc | `smoke-musl-pthread-libc` |
| TLS facade (`arch_set_tls`) | `arch_portable.h`; W10b PTE deferred |
| ARM64 boot stub + `kernel-arm64.bin` link | `smoke-arm64-boot`; `make ARCH=arm64 kernel-arm64.bin` |
| AF_UNIX + TCP loopback + `send`/`recv` | `smoke-stream-sock` (`STREAM_SENDRECV_OK`) |
| `isa-debug-exit` + CAD/RESTART2 tags | `smoke-isa-debug-exit` |
| ARM64 `platform_ops` virt + RPi stub | `arch/arm64/sources/platform.c` (`arm64_rpi_platform_ops`) |
| KTM v1 core (events/scenarios/transport) | `make ktm-run` (boot suite pass=11) |
| KTM `/dev/ktm` + libktm-user | `make ktm-userdev-run` (`fork_wait_signal`) |
| Kernel `[FASE` serial retired | `rg '\[FASE'` = 0; arch-guard `ktm-no-fase` |
| KTM P0/P1 MM scenarios | `ipc.pipe_lifecycle`, `mm.cow_fork`, `mm.vma`, `mm.page_tables`, `mm.steady_state`, `process.exec`, `process.fork_rollback` in `ktm-run` |
| KTM-P2 shell/devfs/OOM hook | `vfs.devfs`, `shell.redir`, `mm.oom_class` in `ktm-run` (pass=11) |
| POSIX-2 SIGHUP on PTY master close | `smoke-posix-sighup-tty` (`PTY_SIGHUP_PGRP`, `POSIX_SIGHUP_TTY_OK`) |
| PERF-1 `sys_gettid` | removed per-call `GETTID` serial spam |
| Init reparent without CRITICAL spam | early-return if no children; detach if no init |
| `fase42_*` → `ir0_mm_*` / `paging_ir0_mm_*` | rename in `mm/paging.*` + callers |
| ARCH-4 boot serial | `CONFIG_DEBUG_BOOT=n`; verbose `[BOOT]` gated |

## Open

| Item | Next proof |
|------|------------|
| FASE→KTM remaining PARTIAL/GAP | [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md) — drain/fb/TCC (44, 52, 54–55); deep OOM reclaim beyond `mm.oom_class` hook |

## Next focus — technical debt + KTM parity

Prefer **ARCH** / remaining GAP after POSIX-2 + KTM-P2 MVP:

| Sprint | Gate | Note |
|--------|------|------|
| ARCH-3 | lifecycle audit | stream/AHCI/PTY — audited 2026-07-10; no new leak fix required |
| ARCH-2 | facade audit | `arch-guard` green |
| PERF-1 | hot paths | `sys_gettid` spam removed; further hot-path work deferred |
| POSIX-2 | job control | `setsid`/`setpgid` + SIGHUP-on-TTY close done |
| KTM-P2 | boot suite | `vfs.devfs` / `shell.redir` / `mm.oom_class` done; fb/userdev COW still open |

## Future / P2 (dedicated oleadas only)

| Item | Next proof |
|------|------------|
| kexec real / S3–S4 / AML `_S5` | beyond PM1a soft-off + ENOSYS stubs |
| ACPI FADT walk beyond 0–48 MiB | identity-map expand to 256 MiB hung init spawn; needs dedicated map |
| AHCI NCQ / NVMe | storage oleada |
| ARM64 userspace / MM | beyond freestanding `kernel-arm64.bin` |
| TCP Internet / real NIC stream | beyond loopback `sock_stream` |
| Rust/C++ driver ABI definitivo | DRV-* |
| SMP / CFS completo | sched oleada |
| T3 WM / compositor / panel | **userspace only** — not in kernel tree |
| TCC hang / Doom “stable” | STABLE.md optional smokes |

## T3 prep checklist (no WM in kernel)

| Prerequisite | Status | Evidence |
|--------------|--------|----------|
| Stream sockets (local) | OK | `smoke-stream-sock` |
| Input events | OK (prior) | T2 path |
| Framebuffer | OK (prior) | T2 path |
| Kernel-side WM | **Out of scope** | T3 rule |
