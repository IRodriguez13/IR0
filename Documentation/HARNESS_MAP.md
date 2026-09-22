# Harness map (semantic tags and Make names)

> **Last verified:** 2026-09-22
> **Source of truth:** `setup/make/legacy-smokes.mk`, `scripts/make/testing.mk`,
> `setup/pid1/ktm_*.c`, `setup/doom/doomgeneric_ir0.c`,
> `scripts/architecture_guard.py` (forbids leftover `[FASE` in kernel C).

Canonical names for QEMU smokes, KTM userdev cases, and serial tags.
Wave-era `FASE*` identifiers are **aliases or external contracts**, not
production names.

Spanish mirror: [`esp/HARNESS_MAP.md`](esp/HARNESS_MAP.md).
Historical class table: [`KTM_FASE_INVENTORY.md`](KTM_FASE_INVENTORY.md)
(stub → this file).

## Serial tags (guest)

C prints semantic tokens. Grep/`--done`/`--require` must match these.

| Area | Tag now | Old wave token (retired in C) |
|------|---------|-------------------------------|
| Heap / brk | `HEAP_SMOKE` | `FASE39*` |
| mmap | `MMAP_SMOKE` | `FASE39*` |
| Fork COW | `MM_COW_*` | `FASE40*` |
| Reclaim / PT | `RECLAIM_*` | `FASE41` / `FASE42` |
| IPC | `IPC_*` | `FASE48` |
| Pipe | `PIPE_SMOKE` | `FASE49` |
| PID1 `_exit` | `INIT_EXIT_DRAIN` | `FASE44` |
| Doom product | `KTM_DOOMGENERIC_OK` / `KTM_DOOMGENERIC_CASE_OK` | `FASE55D_DOOMGENERIC_OK` / `KTM_DOOM_55D_OK` |
| Doom stub | `KTM_DOOM_STUB_*` | `FASE55B*` |

Kernel production paths must not print `[FASE…`. Debug leftover is
`IR0_DEBUG_PROC` (not `CONFIG_DEBUG_FASE50`).

## Make targets (prefer these)

| Semantic target | Legacy alias still works |
|-----------------|--------------------------|
| `smoke-reclaim` | `smoke-userspace-fase41-reclaim` |
| `smoke-fork-exit-storm` | `smoke-fase43-fork-exit-storm` |
| `smoke-fork-wait-storm` | `smoke-fase43-fork-wait-storm` |
| `smoke-exec-loop` | `smoke-fase43-exec-loop` |
| `smoke-fork-wait-drain` | `smoke-fase44-fork-wait-drain` |
| `smoke-exec-drain` | `smoke-fase44-exec-drain` |
| `smoke-init-exit-drain` | `smoke-fase44-init-exit-drain` |
| `smoke-fork-rollback` | `smoke-fase45-fork-rollback-storm` |
| `smoke-fork-mem-touch` | `smoke-fase45-fork-mem-touch` |
| `smoke-fork-no-recursion` | `smoke-fase46-fork-no-recursion` |
| `smoke-fork-heap` | `smoke-fase46-fork-heap` |
| `smoke-ipc` | `smoke-fase48-ipc` |
| `smoke-pipe` | `smoke-fase49-pipe` |
| `smoke-busybox` | `smoke-fase50-busybox` |
| `smoke-exec-only` | `smoke-fase50-exec-only` |
| `smoke-programs` | `smoke-fase50-programs` |
| `smoke-shell` | `smoke-fase51-shell` |
| `smoke-tcc` | `smoke-fase52-tcc` |
| `smoke-fs-dev` | `smoke-fase53a-fs-dev` |
| `smoke-posix-pseudofs` | `smoke-fase53b-posix-pseudofs` |
| `smoke-fbdev` | `smoke-fase54a-fbdev` |
| `smoke-input` | `smoke-fase54b-input` |
| `smoke-input-det` | `smoke-fase54c-input-det` |
| `smoke-doom-prereq` | `smoke-fase55a-doom-prereq` |
| `smoke-doom-stub` | `smoke-fase55b-doom-stub` |
| `smoke-doom-timing` | `smoke-fase55c-timing-input` |
| `smoke-doomgeneric` | `smoke-fase55d-doomgeneric` |
| `smoke-ash-interactive` | `smoke-fase58e-ash-interactive` |
| `smoke-busybox-coreutils` | `smoke-fase58l-busybox-coreutils` |
| `build-busybox-min` | `build-busybox-fase50-min` |
| `build-busybox-plus` | `build-busybox-fase58-plus` |
| `build-busybox-full` | `build-busybox-fase58-full` |
| `build-tcc` | `build-tcc-fase52` |
| `build-doom-interactive` | `build-fase55e-doom-interactive` |
| `run-ash-gui` | `run-fase58e-ash-gui` |
| `check-ash-logs` | `check-fase58e-logs` |

`build-init-fase*` aliases still map onto `build-ktm-*` helpers.

Enable historical recipes with `IR0_LEGACY_SMOKE=1` when the Makefile
requires that flag.

## Generated binaries (IR0 tree)

| Variable / path now | Old path |
|---------------------|----------|
| `setup/pid1/busybox_real` (`BUSYBOX_REAL_BIN`) | `fase50_busybox_real` |
| `setup/pid1/hello_helper` | `fase50_hello` |
| `setup/pid1/ipc_cat` / `ipc_echo` / `ipc_busybox` | `fase48_*` |
| `setup/pid1/tcc_harness` | `fase52_harness` |
| `setup/pid1/busybox_manifest_smoke` | `fase58l_busybox_smoke` |

## Kept FASE strings (do not rename)

| Item | Why |
|------|-----|
| Env `FASE50_BUSYBOX_BIN` | ISD `busybox_inject_manifest.sh` / `busybox_check_manifest.sh` / `pack-minix.sh` |
| ISD `packages/busybox/fase58_*.config` | External package contract |
| `setup/pid1/fase52_staging/` | TCC sysroot; high-risk path |
| `runit_fase55d_*` in ISD stage-bin | Sibling service names |
| `scripts/migrate_fase*.py` | Historical one-shot tools |

## Canonical KTM gates (prefer over legacy QEMU)

| Area | Gate |
|------|------|
| Fork / wait depth | `ktm-userdev-fork-storm-run` |
| Exec drain | `ktm-userdev-exec-drain-virtfs-run` |
| PID1 `_exit` | `ktm-userdev-init-exit-drain-virtfs-run` |
| POSIX pseudo-fs | `ktm-userdev-posix-pseudofs-virtfs-run` |
| Input deterministic | `ktm-userdev-input-det-virtfs-run` |
| BusyBox manifest | `ktm-userdev-busybox-manifest-run` |
| Doom IWAD | `ktm-userdev-doom-55d-run` (alias; requires `KTM_DOOMGENERIC_OK`) |
| COW data-plane | `smoke-mm-cow-lazy` |
| Product PID1 | `smoke-runit-boot` |

## Host logs

Logs under `/tmp/` use semantic names (`/tmp/userspace-ipc.log`,
`/tmp/ash-gui.log`, …). Old `/tmp/userspace-fase*` / `/tmp/fase58*` paths
are no longer written.
