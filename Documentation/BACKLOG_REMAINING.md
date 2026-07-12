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
| F2 AHCI NCQ (FPDMA + multi-slot) | `smoke-ahci-read` → `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED` |
| F3 DSDT `_S5_` SLP_TYP | `ACPI_S5_OK` + PM1a `(typ<<10)\|SLP_EN` |
| F4 kexec stub reboot | `REBOOT_KEXEC_STUB` via `smoke-reboot-kexec-enosys` |
| F5 suspend stub success | superseded by S3 soft resume (`smoke-reboot-s3`) |
| kexec_load MVP | `smoke-kexec-load` → `KEXEC_LOAD_OK` + `REBOOT_KEXEC_LOADED` + `KEXEC_PAYLOAD_OK` |
| S3 soft resume MVP | `ACPI_S3_OK` + `SYSTEM_S3_ENTER` + `SYSTEM_S3_RESUME_OK` (`smoke-reboot-s3`) |
| F6 NVMe detect+read | `smoke-nvme-read` → `NVME_DETECT_OK` + `NVME_READ_OK` |
| P1-storage bundle | `make smoke-p1-storage` (FAT/EXT2/GPT/AHCI/NVMe) |
| Kernel relative includes hygiene | `<kernel/…>` in syscalls/process; arch-guard `[kernel-include]` |
| KTM typed `PIPE_*` events | `KTM_EVENT_PIPE_{CREATE,EOF,EPIPE,WAKE}` in `pipe.c` |
| POSIX-2 setsid/setpgid + SIGHUP/TTY | `smoke-posix-setsid`, `smoke-posix-sighup-tty` |
| Real fork COW A–F (HOST) | `make smoke-mm-cow-lazy` (FASE40 A–F) — verified 2026-07-11 |
| GPT / EXT2 RO / AHCI detect+read + multi | `smoke-gpt-partition`, `smoke-ext2-mount`, `smoke-ahci-read`, `smoke-ahci-multi` |
| FAT16 write + vfs-write audit | `linux-abi-audit-vfs-write-fat` VERIFIED |
| PTY multi + `TIOCSWINSZ` | `smoke-pty-winsz` |
| musl pthread libc | `smoke-musl-pthread-libc` |
| TLS facade (`arch_set_tls`) | `arch_portable.h`; W10b PTE deferred |
| ARM64 boot stub + `kernel-arm64.bin` link | `smoke-arm64-boot`; `make ARCH=arm64 kernel-arm64.bin` |
| ARM64 early MMU identity map (F7.1) | `smoke-arm64-mmu` → `ARM64_MMU_OK` (TTBR0 idmap DRAM+UART) |
| ARM64 VBAR + EL1 SVC (F7.2) | `smoke-arm64-vbar` → `ARM64_VBAR_OK` + `ARM64_SVC_RET_OK` |
| ARM64 EL0 drop + SVC + PSCI off (F7.3) | `smoke-arm64-el0` / `make smoke-arm64` |
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
| F2 | ~~AHCI NCQ~~ | **DONE** 2026-07-11 — `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED` |
| F3 | ~~AML `_S5` SLP_TYP~~ | **DONE** 2026-07-11 — `ACPI_S5_OK` + typed PM1a |
| F4 | ~~kexec mínimo~~ | **DONE** 2026-07-11 — stub + `kexec_load` MVP (`smoke-kexec-load`) |
| F5 | ~~Suspend / S3~~ | **DONE** 2026-07-11 — `_S3_` + soft resume (`smoke-reboot-s3`; FACS wake deferred) |
| F6 | ~~NVMe MVP~~ | **DONE** 2026-07-11 — `smoke-nvme-read` (`NVME_READ_OK`) |
| F7 | ARM64 MM / userspace | **F7.1–F7.3 done** (MMU + VBAR/SVC + EL0); next = full userspace link / musl |
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
