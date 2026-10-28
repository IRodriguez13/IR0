# Harness map (semantic tags and Make names)

> **Last verified:** 2026-10-28
> **Source of truth:** `scripts/make/testing.mk`, `setup/pid1/ktm_*.c`,
> `setup/doom/doomgeneric_ir0.c`, `scripts/architecture_guard.py`
> (forbids leftover `[FASE` in kernel C).

Canonical names for QEMU smokes, KTM userdev cases, and serial tags.
Wave-era `FASE*` identifiers survive only as **external contracts**; the kernel
no longer prints them.

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
| ARM product VFS | `ARM64_PRODUCT_VFS_OK` | — |
| ARM early `/tmp` fold | `ARM64_EARLY_PATH_FOLD_OK` | — |
| ARM heap/owned mm | `ARM64_PROCESS_MM_HEAP_OK` / `OWNED_OK` / `RETIRE_OK` / `EXIT_OK` | — |

Kernel production paths must not print `[FASE…`. Debug leftover is
`IR0_DEBUG_PROC` (not `CONFIG_DEBUG_FASE50`).

## Make targets

The historical `smoke-fase*` / `build-*-fase*` alias catalog was removed on
2026-10-28 together with `setup/make/legacy-smokes.mk`. Only the canonical
names exist; there is no `IR0_LEGACY_SMOKE` gate.

| Area | Target |
|------|--------|
| Tier-1 boot + ash | `smoke-tier1`, `run-ash-gui`, `check-ash-logs` |
| runit product boot | `smoke-runit-boot`, `smoke-runit-login`, `smoke-runit-power` |
| BusyBox | `build-busybox-min`, `build-busybox-plus`, `build-busybox-full` |
| TinyCC | `build-tcc` |
| KTM in-guest smokes | `ktm-userdev-*-run` pilots (`tests/ktm/userdev/`) |
| COW data-plane | `smoke-mm-cow-lazy` |
| ARM64 | `smoke-arm64-all` and the per-slice `smoke-arm64-*` targets |

`build-init-*` targets compile the ring-3 init that `make load-init` injects;
`make build-init-musl` produces the musl static build of `init_musl.c`.

## Generated binaries (IR0 tree)

| Variable / path now | Old path |
|---------------------|----------|
| `setup/pid1/busybox_real` (`BUSYBOX_REAL_BIN`) | `fase50_busybox_real` |
| `setup/pid1/busybox_manifest_smoke` | `fase58l_busybox_smoke` |

## Kept FASE strings (do not rename)

| Item | Why |
|------|-----|
| Env `FASE50_BUSYBOX_BIN` | ISD `busybox_inject_manifest.sh` / `busybox_check_manifest.sh` / `pack-minix.sh` |
| ISD `packages/busybox/fase58_*.config` | External package contract |
| `setup/pid1/fase52_staging/` | TCC sysroot; high-risk path |
| `runit_fase55d_*` in ISD stage-bin | Sibling service names |

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
