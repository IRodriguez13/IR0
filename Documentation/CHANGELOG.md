# IR0 Kernel Changelog

> **Last verified:** 2026-07-21  
> **Source of truth:** git history, `make ktm-check`, roadmap smokes in `Makefile`, [`HARDENING.md`](HARDENING.md), [`KTM.md`](KTM.md)

This file tracks user-visible and developer-facing changes per iteration.
For tier backlog see [`ROADMAP.md`](ROADMAP.md). For **what is stable in QEMU** see [`STABLE.md`](STABLE.md).

## [Unreleased]

### Logging / KTM hygiene (2026-07-21)

- **klog hub** — human serial format `[ts] [LEVEL] [COMP] msg` in `ktm/klog.c`; facade `<ir0/ktm/klog.h>` (legacy `includes/ir0/klog.c` removed).
- **ASSERT_BATCH** — collapse happy-path soft asserts (`process.wait_drain` / `reclaim_exit`) into one `KTM|…|ASSERT_BATCH|…` line.
- **`CONFIG_KTM_SERIAL_VERBOSE`** — default **n**; product/desk boots skip noisy CHECKPOINT/PROBE mirrors.
- **Autokill** — QEMU host stderr → sibling `*.qemu-stderr` (guest serial log stays clean).
- **runit** — `ir0_smoke_tag.h` + `runit_hostshare_payload_run` / `runit_pause_run` for hybrid 9p desk/KTM smokes.
- **Docs** — `KTM.md` logging layers; `ai_driven_dev/rules/ir0-version-stamp.mdc` (lockstep `version.h` ↔ upstream tags).

---

## [0.0.1] — 2026-06-23 — stable baseline + hardening closed

### Documentation

- **`Documentation/STABLE.md`** (+ `esp/STABLE.md`) — release 0.0.1 checklist: formerly in-dev items closed, roadmap achievements testable in QEMU (serial + GTK), explicit non-goals.
- **`ROADMAP.md`**, **`HARDENING.md`**, **`README.md`** — aligned with H1–H6 done and 0.0.1 scope.
- **`make health`** — includes `kernel-text-budget`.

### Hardening (H1–H6 closed)

- **H1** — `kernel/syscalls.c` 86 lines; submodules under `kernel/syscalls/`.
- **H2** — FASE43–48 in `kernel/debug/fase_audit.c`; `process.c` ~3014 L; `process_reap_zombie_child()` production path.
- **H3** — 0 `#include <drivers/` in `includes/ir0/` (arch-guard rule 14).
- **H4** — `test_musl_cred_abi`, `includes/ir0/abi/musl_cred_abi.h`.
- **H5** — `devfs_resolve_read_fd()`; dead block_dev stubs removed.
- **H6** — `make kernel-text-budget` (~815754 B / cap 850000); USB describe fix.

### Release baseline (maintainer sign-off)

Closed for 0.0.1 without re-smoke unless regression: **runit**, **BusyBox ash/applets**, **TinyCC**, **COW fork**, **lazy alloc**, tier1 POSIX smokes, T2 fb/input/Doom GUI targets.

### Permissions & lifecycle (included in 0.0.1)

- **`ir0_access_from_stat_groups()`** — supplementary group checks.
- **ARCH-3** — devfs FD release; fork rollback FD cleanup.
- **ARCH-4** — FASE50-gated exit/destroy serial noise.

### ktest / block / devfs

- **ktest open ABI** — `KTEST_O_*` Linux flags.
- **ATA sector count** — IDENTIFY + `ata_get_size()`.
- **devfs read** — per-FD offset; unified `sys_read` path.

### Validation

| Check | Result |
|-------|--------|
| `make kernel-tests` | 29/29 |
| `make -C tests/host run` | 12/12 |
| `make arch-guard` | OK |
| `make build-matrix-min` | OK |
| `make kernel-text-budget` | OK |
| `make smoke-tier1` | prior green |

---

## [0.0.1-pre] — 2026-06-23 hardening oleada (pre-doc consolidation)

### Permissions & lifecycle (T0/T1)

- **`ir0_access_from_stat_groups()`** — supplementary group checks for path permission facades (`fs/permissions.c`, `kernel/credentials.c`, `includes/ir0/path_routed.c`, `kernel/syscalls.c`).
- **ARCH-3** — `process_release_fds()` closes devfs nodes; `fork_rollback()` releases child FD table on failure.
- **ARCH-4** — exit/destroy/exec-fail serial storms gated with `CONFIG_DEBUG_FASE50`.

### ktest / block / devfs fixes

- **ktest open ABI** — `KTEST_O_*` Linux flags in `kernel/test/ktest_harness.h`; fixes mount tmpfs/multi-fs/longest-prefix contracts (see [`HARDENING.md`](HARDENING.md)).
- **ATA sector count** — `ata_devices[].size` populated on IDENTIFY; `ata_get_size()` fallback from `capacity_bytes`.
- **devfs** — `sys_read` honors per-FD offset for bound devfs files.
- **`sys_mount`** — suppress misleading stderr on `-EBUSY`.

### Structural hardening (H2/H5/H6)

- **`process_t`** — FASE44/46 audit fields removed; side table in `kernel/debug/fase_audit.c`.
- **`sys_read`** — single devfs path via `devfs_resolve_read_fd()`.
- **`make kernel-text-budget`** — `.text` cap 850000 B (current ~822202 B).

- **`kernel/syscalls.c`** — 86 lines (glue only); logic in `kernel/syscalls/*` (`fs_path_syscalls`, `validate_user`, extended mm/process/io).
- **H3 partial** — `includes/ir0/partition.h`, `block_dev.h` decoupled from `<drivers/...>`.
- **H4** — `test_musl_cred_abi` + `includes/ir0/abi/musl_cred_abi.h`; host 12/12.
- **H5/H6** — dead block_dev stubs removed; USB describe fix; FASE46 gated.

### Validation (post-ARCH-1)

- `make kernel-tests` — 29/29; `runtime-mount-check`; arch-guard; build-matrix-min; host 12/12.
- `make smoke-runit-boot` — 3/3 green.

---

## [0.0.0-pre] — 2026-06-17 iteration

### Stability & POSIX (T1)

- **wait4(pid, NULL, …)** — Fixed hang in FASE40 / `smoke-mm-cow-lazy`: arch context switch now resumes blocked waiters when `wait_status_ptr` is NULL but `irq_frame_saved` is set; wake path aligned in `process_exit` and `syscall_wake_blocked_on_child`.
- **init_fork_mem_smoke** — `touch_mmap_pages()` marked `noinline` to avoid `-Os` codegen breaking lazy COW touch.

### Build / CI

- **`CONFIG_TICK_RATE_HZ`** — `drivers/timer/clock_system.c` includes `config.h`; `Makefile` always passes `-DCONFIG_TICK_RATE_HZ` (default 1000). Fixes `kernel-x64-test.bin` build without `.config`.

### ARCH-1 syscall split

- **`kernel/syscalls/time_syscalls.c`** — `sys_gettimeofday`, `sys_clock_gettime` extracted from `kernel/syscalls.c`.
- **`kernel/syscalls/syscall_dispatch.c`** — Linux x86-64 syscall table + `syscall_dispatch()` (~400 lines moved out of monolith).

### Documentation (stable contracts)

- **`Documentation/CHANGELOG.md`** — iteration log (EN + `esp/` mirror).
- **`Documentation/ai_driven_dev/ktm.md`** — KTM panic site, macros, inventory gate.
- **`PROCESSES.md`**, **`MEMORY.md`**, **`TOOLING.md`**, **`VIRTUAL_FILESYSTEMS.md`**, mandocs `process`/`interrupts` — aligned with wait4 NULL, COW, `/dev/hda`, panic path.

### Storage / devfs

- **`fs/devfs.c`** — `dev_disk_read` fix for QEMU IDENTIFY.
- **`fs/fat16_disk.c`** — read-only FAT16 on `hda` / `hda1`…; `fat16_fs.c` routes virtual `fat0` vs block devices.
- ktest `fat16_bpb_probe`.

### KTM (Kernel Trace Module)

- **`panic()` → macro** — `includes/ir0/oops.h` maps legacy `panic(msg)` to `panicex` with **callsite** `__FILE__` / `__LINE__` / `__func__` (removed wrapper in `oops.c` that always reported `oops.c`).
- **`PANIC_HW` / `PANIC_MEM`** — Level-specific panic macros for hardware and OOM paths.
- **Fault path** — `arch/x86-64/sources/fault.c`, HPET, arch divide-by-zero use `PANIC_HW` where appropriate.
- **`[KTM][PANIC_SITE]`** — `ktm_panic_site_emit()` logs file, line, caller, and message on every `panicex`.
- **`ktm_classify_kernel_panic_ex()`** — Classification uses panic level first, then message heuristics (`KERNEL_HW_FAULT`, `KERNEL_OOM`, etc.).
- **`scripts/ktm_panic_inventory.py`** — CI gate in `make ktm-check`; host test `test_ktm_panic_inventory_contract`.

### Validation (this iteration)

| Check | Result |
|-------|--------|
| `make kernel-x64.bin` | OK |
| `make ktm-check` | OK |
| `kernel-tests` (32/32) | prior green |
| `smoke-mm-cow-lazy`, tier1 smokes | prior green |
| `tests/host` | 10 tests incl. panic inventory |
| `syscalls.c` line count | ~3536 (was ~3950; dispatch extracted) |

### Known gaps (historical snapshot — see [0.0.1] / [`STABLE.md`](STABLE.md) for current)

- ~~P1-storage: FAT16 on `block_dev`~~ — MVP read-only in 0.0.1
- ~~ARCH-1: further `syscalls.c` split~~ — done H1 (86 L)
- POSIX-1: futex robust / musl pthread edge cases — still open
- Audit noise on some lazy-touch paths — gated `IR0_DEBUG_PROC`

---

## Format

New entries go under `[Unreleased]` at the top. On a tagged release, rename the section to the version/date and open a fresh `[Unreleased]`.
