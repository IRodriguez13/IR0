# IR0 — Post-0.0.1 backlog (honest remaining work)

> **Last verified:** 2026-07-12  
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
| AF_UNIX `socketpair` stream | `smoke-socketpair` (`SOCKETPAIR_OK` / `KTM_SOCKETPAIR_OK`) |
| AF_UNIX abstract + sock poll | `smoke-unix-abstract` (`UNIX_ABSTRACT_OK` / `SOCK_POLL_OK`) |
| `SCM_RIGHTS` sendmsg/recvmsg | `smoke-scm-rights` (`SCM_RIGHTS_OK`) |
| SysV shm (MIT-SHM prep) | `smoke-sysv-shm` (`SYSV_SHM_OK`) |
| `mmap` MAP_SHARED `/dev/fb0` | `smoke-fb-map-shared` (`FB_MAP_SHARED_OK`) |
| Host-share virtio-9p (QEMU `-virtfs`) | `smoke-hostshare-9p` (`HOSTSHARE_9P_OK` / `KTM_HOSTSHARE_OK`) |
| Host-share exec (stub + `ir0_payload`) | `smoke-hostshare-exec` (`HOSTSHARE_EXEC_MOUNT_OK` + case done tag) |
| `isa-debug-exit` + CAD/RESTART2 tags | `smoke-isa-debug-exit` |
| ARM64 `platform_ops` virt + RPi stub | `arch/arm64/sources/platform.c` |
| KTM boot suite | `make ktm-run` (pass=16 incl. `process.reclaim_exit`) |
| KTM userdev | `ktm-userdev-run`, `ktm-userdev-cow-run` |
| Kernel `[FASE` serial retired | arch-guard `ktm-no-fase` |
| PERF-1 `sys_gettid` | no per-call GETTID spam |
| FASE→KTM Open residual | 41/42/44 fork+exec_drain+reap_drain+**init_exit_drain** SUB; 52/55/58 HOST+KTM; **57 GUI** HOST |

## Open

| Item | Blocks | Next proof |
|------|--------|------------|
| Maintainer manual VM (**mantenedor only**) | **0.0.1 ship** | Interactive QEMU GTK / serial — **not agent backlog** |
| ARM64 `ALL_OBJS` + musl aarch64 | F7b port | Cross toolchain + KERNEL_OBJS link |
| virtiofs + FUSE | Future host-share | Guest FUSE; 9p remains ship path |

## Closed this wave (2026-07-12) — F8-3 wire TCP

| Item | Proof |
|------|-------|
| **F8-3** minimal wire TCP | `net/tcp.c` SYN/SYN-ACK/ACK + PSH; `sock_stream` wire path; `make smoke-tcp-wire` → `F8_TCP_WIRE_OK` + host listener `WIRETCP` |

Slice: connect+one-shot send to QEMU gateway **10.0.2.2:8888** (no retransmit/full stack). Not 0.0.1 ship gate.

## Closed this wave (2026-07-12) — Host-share exec + F8 harden + FAT ship note

| Item | Proof |
|------|-------|
| 9p ELF-sized I/O | `virtio_9p_stat_file` / chunked `virtio_9p_read_file`; `hs_stat`/`hs_read` |
| Stub + KTM share-payload | `init_hostshare_exec`; runner default; `make smoke-hostshare-exec` |
| F8 harden | NET RX `LOG_DEBUG`; FIN\|ACK + bounded poll; `smoke-nic-reach` / `smoke-tcp-guest` / `smoke-tcp-wire` green |
| FAT + MINIX ship | MINIX = root; FAT16 = secondary (`smoke-fat16-mount`); **no** FAT-as-root |

## Closed this wave (2026-07-12) — BUSY-1/2

| Item | Proof |
|------|-------|
| **BUSY-1** product applet manifest | `setup/busybox/required_applets.txt` + `scripts/busybox_inject_manifest.sh` on runit disk (`load-userspace-runit`) |
| **BUSY-2** applet smoke | `make smoke-busybox-manifest` → `BUSYBOX_MANIFEST_OK` (also `smoke-fase58l-busybox-coreutils`) |

virtiofs/FUSE remains **Future** (no guest FUSE). Host-share remains virtio-**9p**.

Tag `v0.0.1-rc2` = automated critical gates only; **ship** still needs Maintainer manual VM above.

## ARM64 — honest status (2026-07-12)

**What works (freestanding QEMU virt only):**

```text
UART → identity MMU → VBAR → EL1 SVC → EL0 drop → EL0 SVC → PSCI OFF
Proof: make smoke-arm64
```

**What does not exist yet (real port):**

| Gap | Today |
|-----|--------|
| Full kernel link | `kernel-arm64.bin` = `ARCH_OBJS` stubs, not `ALL_OBJS` |
| Syscall dispatch / VFS / MM | x86-only production paths |
| Context switch | `switch_arm64.c` empty stub |
| musl / BusyBox / init | no ARM rootfs |
| GIC / timer / virtio / storage | not wired in boot image |
| Board beyond QEMU virt | RPi ops stub only |

**Rough maturity:** early CPU privilege bring-up (**not** “ARM port ~done”). Treat F7 as
closed for that slice; track real port as **F7b** above.

**F7b.1 (2026-07-12):** curated `ARM64_SLICE_OBJS` (`slice_hello`) linked post-MMU →
`ARM64_SLICE_OK`; `make arm64-slice-compile` also probes `includes/string.c` (compile-only,
not linked — still pulls oops headers). Not `ALL_OBJS`.

**F7b pack (2026-07-12):** PL011 TU + `serial_io_arm64` + early paging API verify +
GIC Device map (no IRQ program) + arch timer CNTFRQ/CNTPCT.
Proof: `make smoke-arm64-port` → `ARM64_PL011_OK` + `ARM64_PAGING_OK` + `ARM64_GIC_MAP_OK`
+ `ARM64_TIMER_OK`. Meta `make smoke-arm64` includes port smoke.

**F7b GIC/IRQ + serial + portable (2026-07-12):** GICv2 Dist/CPU + CNTP PPI30 one-shot
→ `ARM64_GIC_OK` / `ARM64_TIMER_IRQ_OK` (`smoke-arm64-gic`, QEMU `gic-version=2`); remask
before EL0. COM1 `serial.o` excluded when `ARCH=arm64`; `serial.c` x86-guarded.
`make arm64-portable-compile` grows curated objs (`gic_v2`/`timer`/`portable_string`).

**BLOCKED — ALL_OBJS / musl aarch64:** Pack E cleared the **INTERRUPT_OBJS** wall
(`INTERRUPT_OBJS_ARM64` = `irq_portable_stubs.o`). `make arm64-all-objs-probe` compiles
`MEMORY_OBJS` + sample **`kernel/main.c`** / **`open_flags.c`** OK; **next divergence** =
full KERNEL_OBJS / drivers (still not linked as `kernel-arm64.bin = ALL_OBJS`).
**musl aarch64** still **BLOCKED** (SETUP has x86 musl only).

**F8-facade-mm (2026-07-12):** `arch_mm_activate` / `arch_mm_current_root` /
`arch_tlb_invalidate_*` / `arch_irq_save|restore` in `arch_portable`; x86 + ARM64
`mm_ops.c`. Portable callers (`mm/paging` wrappers, `mm_syscalls`, `rr_sched`,
`process_irq_*`) no longer embed `%%cr3`/`invlpg`/`pushfq`. PTE walker still x86
in `mm/paging.c`. **ALL_OBJS/musl aarch64 still BLOCKED.**

**F8b irq/MM residual sweep (2026-07-12):** Mechanical migration — `pushfq`/`cli`/`popfq`
→ `arch_irq_save`/`arch_irq_restore` in `kernel/{ipc,input_events,sock_udp}.c`,
`net/{udp,dns,dhcp,icmp,arp}.c`, `includes/ir0/{named_fifo,console}.c`,
`drivers/net/rtl8139.c`; `fault.c` → `arch_tlb_invalidate_page`; `acpi_pm.c` →
`arch_mm_current_root`; `idt_arch_x64` → `arch_mm_activate`. Portable trees have no
real `__asm__` pushfq/cr3/invlpg (oops diagnostic + walker + process ABI deferred).
Proof: `kernel-x64.bin` + `arch-guard` + `build-matrix-min` + `tests/host` +
`smoke-stream-sock` + `smoke-arm64-syscall`. Remaining debt: PTE walker / `%%cr0`/`%%cr4`
in `mm/paging.c`; `task.cr3` / fork frames / `switch_x64.asm`.

**F7c (2026-07-12):** EL0 Linux-ish SVC ABI (`x8` nr) — getpid/write/exit + one 4 KiB EL0-RW
page @ `0x42000000`. Proof: `make smoke-arm64-syscall` → `ARM64_EL0_PAGE_OK` +
`ARM64_WRITE_OK` + `ARM64_SYSCALL_OK`. Not `process_t` / fork / TTBR-per-task.

**F7d nanosleep + Pack A (2026-07-12):** EL0 `nanosleep` (Linux nr 101) busy-wait on CNTPCT;
tag `ARM64_NANOSLEEP_OK`. Boot IRQ demo + freestanding stubs use `arch_irq_*` (`mm_ops`
linked in freestanding image). ARCH-5: `tests/host` `arch_irq_facade_nested`. Proof:
`smoke-arm64-syscall` (+ NANOSLEEP), `tests/host` 20/20, `smoke-mm-cow-lazy`. Remaining:
walker / process ABI / ALL_OBJS/musl; TCC gate separate (see stab notes).

**Pack B / W10b partial + F7e (2026-07-12):** `arch_mm_read_ctrl0` / `write_ctrl0` / `read_ctrl1`
(x86 CR0/CR4; ARM SCTLR_EL1 / 0) — `mm/paging.c` no longer embeds `%%cr0`/`%%cr4`. F7e
`clock_gettime(CLOCK_MONOTONIC)` → `ARM64_CLOCK_GETTIME_OK`. arch-guard
`[portable-no-isa-asm]` bans pushfq/`%%cr3`/invlpg in portable trees (allowlist `oops.c`).
Product gates (no master merge this wave): `smoke-tcc-power-halt`,
`IR0_LEGACY_SMOKE=1 smoke-fase55b-doom-stub`, `smoke-posix-depth` + CTR/cow/arm64. Remaining:
PTE walker algorithm still x86 in `mm/paging.c`; process ABI; ALL_OBJS/musl.

**Pack C / walker indices + F7f (2026-07-12):** `arch_mm_va_indices` + `arch_mm_pte_present` /
`large` / `phys`; walk paths in `paging_get_pte` / `is_page_mapped` / `map_page_in_directory` /
`unmap_page_in_directory` use facade (no hardcoded `>>39` in those paths). F7f
`gettimeofday` → `ARM64_GETTIMEOFDAY_OK`. arch-guard also bans `%%cr0`/`%%cr4` asm in portable.
Proof: cow + arm64-syscall + stream-sock + CTR. Remaining: `get_or_create_table` / full map
ISA split; process ABI; ALL_OBJS/musl.

**Pack D / PTE encode + F7g (2026-07-12):** `arch_mm_make_table_pte` / `make_leaf_pte` /
`pte_set_user` used in `get_or_create_table` + leaf `map_page_in_directory`. F7g
`clock_nanosleep` (nr 115) → `ARM64_CLOCK_NANOSLEEP_OK`. ARCH-5 host:
`test_arch_mm_pte_facade`. Proof: CTR + `smoke-mm-cow-lazy` + `smoke-arm64-syscall` +
`smoke-stream-sock`. Remaining: process ABI (`task.cr3` / fork); full ARM walker;
ALL_OBJS/musl **BLOCKED**. No master merge this wave.

**Pack E / process MM-root + INTERRUPT wall + probe (2026-07-12):**
`task_mm_root` / `process_mm_root` accessors (field `cr3` @ +0xB0 unchanged for asm).
`mm/paging.c` residual present-checks → `arch_mm_pte_present`. Makefile
`INTERRUPT_OBJS_ARM64` = `irq_portable_stubs.o` (no `lidt` / `isr_stubs_64`).
`make arm64-all-objs-probe`: MEMORY_OBJS compile OK under ARM64_BOOT_CFLAGS;
**next divergence** = KERNEL_OBJS / drivers (not linked into `kernel-arm64.bin`).
**musl aarch64** still **BLOCKED** (no cross toolchain in SETUP; x86 musl only).

---

| # | Item | Next proof |
|---|------|------------|
| F0 | ~~Host-share virtio-9p~~ | **DONE** 2026-07-12 — `smoke-hostshare-9p` (guest `/mnt/host` → host dir; **not** virtiofs/FUSE) |
| F1 | ~~ACPI FADT map seguro~~ | **DONE** 2026-07-11 — `ACPI_FADT_MAPPED` + `ACPI_PM1A_POWEROFF` |
| F2 | ~~AHCI NCQ~~ | **DONE** 2026-07-11 — `AHCI_NCQ_OK` / `AHCI_NCQ_UNSUPPORTED` |
| F3 | ~~AML `_S5` SLP_TYP~~ | **DONE** 2026-07-11 — `ACPI_S5_OK` + typed PM1a |
| F4 | ~~kexec mínimo~~ | **DONE** 2026-07-11 — stub + `kexec_load` MVP (`smoke-kexec-load`) |
| F5 | ~~Suspend / S3~~ | **DONE** 2026-07-11 — `_S3_` + soft resume (`smoke-reboot-s3`; FACS wake deferred) |
| F6 | ~~NVMe MVP~~ | **DONE** 2026-07-11 — `smoke-nvme-read` (`NVME_READ_OK`) |
| F7 | ARM64 early bring-up (F7.1–F7.3) | **DONE** `make smoke-arm64` — **not** full port |
| F7b | ARM64 real port | F7c–F7g + Pack B–E; KERNEL_OBJS link + musl aarch64 **BLOCKED** |
| F8 | TCP Internet / real NIC (**0.0.2**) | F8-1/F8-2/F8-3 **PARTIAL** — `smoke-nic-reach`, `smoke-tcp-guest`, `smoke-tcp-wire`; no full TCP stack (retransmit, listen, teardown) |
| F9 | SMP / CFS | **much later** — not coupled to UI/X11 |
| F10 | Rust/C++ driver ABI | DRV-* |
| F11 | T3 WM / X11 userspace | **userspace only**, after usable net + T2; not with SMP |
| F12 | TCC/Doom “stable” | STABLE.md — merge master solo con bundle verde (Doom=**55d IWAD**) |
| F13 | BusyBox product applets (**BUSY-1/2**) | **DONE** 2026-07-12 — `smoke-busybox-manifest` (`BUSYBOX_MANIFEST_OK`); ship still needs maintainer VM |

## T3 prep checklist (no WM in kernel)

| Prerequisite | Status | Evidence |
|--------------|--------|----------|
| Stream sockets (local) | OK | `smoke-stream-sock` |
| `socketpair` / abstract unix | OK | `smoke-socketpair`, `smoke-unix-abstract` |
| `SCM_RIGHTS` | OK | `smoke-scm-rights` |
| SysV shm (MIT-SHM) | OK | `smoke-sysv-shm` |
| MAP_SHARED `/dev/fb0` | OK | `smoke-fb-map-shared` |
| poll on stream socks | OK | `SOCK_POLL_OK` in `smoke-unix-abstract` |
| Input events | OK | T2 path |
| Framebuffer | OK | T2 path |
| Kernel-side WM | **Out of scope** | T3 rule |
| X11 userspace | **Next campaign** | F11 — outside kernel tree |
