# IR0 — Tree reorganization plan (post–0.0.1)

> **Last verified:** 2026-06-27  
> **Status:** PLAN ONLY — no file moves until release 0.0.1 capability gates are stable  
> **Source of truth:** repository layout, `Makefile`, `scripts/make/*.mk`, `setup/pid1/`, `tests/`

---

## 1. Purpose

Prepare a **maintainable tree** for the decade after 0.0.1: kernel code, QA/tooling,
documentation, and historical experiments clearly separated. The **root Makefile** must
reflect **daily developer commands**, not the full history of tier smokes (fase40–fase58).

This document does **not** authorize moves yet. Execute after:

- `wait4_wnohang` audit stable (current release critical path)
- Process lifecycle bundle deferred until then
- Release 0.0.1 capability board targets met or explicitly scoped down

---

## 2. Current state (snapshot 2026-06-27)

### 2.1 Top-level layout

| Path | Role today | ~source files | Notes |
|------|------------|---------------|-------|
| `kernel/` | Core kernel + tests + debug | 70 C/H | `kernel/test/` = in-kernel ktest; `kernel/syscalls/` split started |
| `arch/` | Ports x86-64, arm64 | 27 | |
| `mm/`, `fs/`, `net/`, `sched/` | Portable subsystems | 56 | Correct layer |
| `drivers/` | HW drivers | 70 | |
| `includes/ir0/` | Facades + some `.c` impl | 131 | Mixed headers and implementation |
| `debug_bins/` | Tier-0 syscall shell | 61 | Userspace-in-tree |
| `setup/pid1/` | Init/smokes/staging | 110 | **Heavy historical fase\*** + tier-1 production |
| `setup/third-party/`, `busybox/`, `doom/`, `runit/` | Rootfs/toolchain | large | Vendored + experiments |
| `scripts/` | Kconfig, linux_abi, smokes | 83 py/sh | `scripts/make/qa.mk` **1564 lines** |
| `tests/host/` | Host unit tests | stable | |
| `tests/kernel_memsafe/` | MM safety | small | |
| `tests/smoke/` | 1 script | minimal | Underused vs `setup/pid1` |
| `ktm/` | Diagnostics (untracked/partial) | 16 | Wired in **uncommitted** Makefile hunks |
| `Documentation/` | Mandocs, releases, esp | many | Mandoc bulk churn in working tree |

### 2.2 Makefile architecture

| File | Lines | Role |
|------|-------|------|
| `Makefile` (committed at `b4bdeee`) | ~2900+ | OBJ lists, arch, kernel link, **partial** split |
| `Makefile` (working tree) | ~1656 | **−1708** lines moved to includes; adds ktm/futex/sock_udp |
| `scripts/make/run.mk` | 63 | Daily: `run`, `run-dbgshell`, `run-pid1` (uncommitted) |
| `scripts/make/legacy-run.mk` | 78 | Gate `IR0_LEGACY_SMOKE` (uncommitted) |
| `scripts/make/qa.mk` | 1564 | QA, linux-abi-audit, tier smokes include |
| `setup/make/legacy-smokes.mk` | 1394 | **46+** `smoke-fase*` / `smoke-userspace-*` (uncommitted) |

**Partial migration already in working tree** (not on `dev` at B/G commits): main Makefile
`include`s `run.mk` / `legacy-run.mk` / `qa.mk` behind `IR0_INCLUDE_QA` and
`IR0_LEGACY_SMOKE`. Committed history still monolithic for many clones.

### 2.3 QA / test surfaces

| Surface | Location | Gate |
|---------|----------|------|
| In-kernel ktest | `kernel/test/` | `make kernel-tests` |
| Host tests | `tests/host/` | `make -C tests/host run` |
| Linux ABI audit | `scripts/linux_abi/` | `make linux-abi-audit-*` |
| Phase smokes | `setup/pid1/init_fase*.c` + `legacy-smokes.mk` | `IR0_LEGACY_SMOKE=1 make smoke-fase*` |
| Python smokes | `scripts/smoke_*.py` | Via qa.mk |
| KTM / D1 diag | `ktm/`, `scripts/ktm_*` | Ad hoc; not release gates |

### 2.4 Working tree health

- **~562** entries dirty/untracked after commits B/G (bulk oleada: headers, drivers, docs, ktm).
- **Risk:** any reorganization before cleaning triage will amplify merge pain.
- **Positive:** B/G commits are **bisectable** and isolated on `dev` (`0ae449b`, `977b6d0`).

---

## 3. Problems detected

### P0 — Release blockers (fix before any tree move)

1. **Makefile split uncommitted** — daily vs legacy vs QA exists only in working tree.
2. **qa.mk default `qa:` target** (working tree) pulls `linux-abi-audit-kill-sigterm` — wrong for current scope.
3. **`wait4_wnohang` audit** must reach stable PASS before process lifecycle bundle.
4. **ktm/D1 objects** linked from uncommitted Makefile — non-bisectable if mixed into release commits.

### P1 — Structural debt

1. **Historical phases in `setup/pid1/`** — 80+ `init_fase*` / `fase*` smokes coexist with production `irinit.c`, `init_musl.c`.
2. **Makefile as archaeology** — fase targets, audit, rootfs, doom, runit in one graph.
3. **`scripts/make/qa.mk` too large** — mixes linux-abi-audit, release-0.0.1, tier smokes, ktm.
4. **`debug_bins/` vs real userspace** — tier-0 cmds live in-tree; no clear boundary vs future rootfs.
5. **`includes/ir0/*.c`** — implementation files inside include tree blur facade policy.
6. **`tests/smoke/` unused** — smokes live under `setup/pid1` and `scripts/`.
7. **Documentation vs code drift** — mandoc bulk edits uncommitted; Capability Board ahead of stable audits.

### P2 — Nice-to-have after 0.0.1

1. Rename `debug_bins/` → `userspace/cmd/` or keep path with alias.
2. Consolidate `setup/runit`, `setup/doom` under `setup/experiments/`.
3. Generate QA target list from `contracts.json` (single source).

---

## 4. Proposed target tree

Design principle: **kernel tree builds a kernel**; everything that runs in QEMU as test harness
lives under **`qa/`** or **`setup/`**; **documentation** stays under **`Documentation/`**.

```text
IR0/
├── Makefile                    # Thin root (~200–400 lines): arch, config, include mk fragments
├── README.md  SETUP.md
│
├── kernel/                     # Kernel proper only (no ktest inside long-term)
├── arch/
├── mm/  fs/  net/  sched/
├── drivers/
│
├── includes/ir0/               # Headers + facades ONLY (migrate *.c out over time)
│
├── userspace/                  # NEW umbrella (optional rename from debug_bins/)
│   ├── debug_bins/             # Symlink or move: tier-0 syscall cmds + dbgshell
│   └── probes/                 # Future: shared probe helpers (not linux_abi workloads)
│
├── qa/                         # NEW — all quality gates & harnesses
│   ├── linux_abi/              # Move from scripts/linux_abi/
│   ├── ktest/                  # Move from kernel/test/ (or symlink phase 1)
│   ├── smokes/
│   │   ├── legacy/             # fase* QEMU smokes (from setup/pid1/init_fase*.c lists)
│   │   ├── tier1/              # runit-ash, busybox-real, release-0.0.1
│   │   └── scripts/            # smoke_*.py from scripts/
│   ├── host/                   # Move from tests/host/ OR keep tests/host with qa/Makefile glue
│   ├── memsafe/                # From tests/kernel_memsafe/
│   └── make/
│       ├── daily.mk            # run, build kernel, disk helpers
│       ├── audit.mk            # linux-abi-audit-* only
│       ├── legacy.mk           # IR0_LEGACY_SMOKE=1 entire include
│       └── release-0.0.1.mk    # smoke-release-0.0.1, capability gates
│
├── scripts/                    # Dev/build tooling NOT gated as tests
│   ├── kconfig/
│   ├── rootfs/                 # inject_*, verify_minix, load_init wrappers
│   ├── architecture_guard.py
│   └── wrappers/               # ir0-qa.sh, ctr.sh, run_pid1.sh
│
├── setup/                      # Rootfs & toolchain (production path clear)
│   ├── pid1/                   # SHRINK: irinit, init_musl, minimal smokes only
│   ├── rootfs/                 # staging layouts (fase52_staging → archived)
│   ├── busybox/
│   ├── third-party/
│   └── experiments/            # doom, runit, tcc — non-release
│
├── diagnostics/                # NEW — ktm, D1.* (CONFIG_KTM only, never default qa)
│   └── ktm/
│
├── Documentation/
│   ├── releases/
│   ├── mandocs/
│   └── TREE_REORGANIZATION_PLAN.md  (this file)
│
└── tests/                      # DEPRECATED wrapper → qa/ (compat symlinks)
    └── README → ../qa/README.md
```

### 4.1 Root Makefile (target)

```makefile
# Pseudocode — daily developer surface
include scripts/config.mk          # ARCH, .config, toolchains
include qa/make/kernel.mk          # kernel-x64.bin, iso, objs
include qa/make/daily.mk           # run, run-pid1, create-disk

# Optional layers (explicit env):
# IR0_INCLUDE_QA=1      → qa/make/audit.mk + qa/make/release-0.0.1.mk
# IR0_LEGACY_SMOKE=1    → qa/make/legacy.mk
```

**`make help`** lists only: build, run, run-pid1, menuconfig, test (host+ktest), audit (pointer).

### 4.2 Backward compatibility

| Old command | Compatibility layer |
|-------------|---------------------|
| `make smoke-fase50-busybox` | `IR0_LEGACY_SMOKE=1 make smoke-fase50-busybox` → alias in `qa/make/legacy.mk` |
| `make kernel-tests` | Alias to `qa/ktest` runner or keep target in `kernel.mk` |
| `scripts/linux_abi/*` | Symlink `scripts/linux_abi` → `qa/linux_abi` for one release cycle |
| `make -C tests/host run` | Keep until `make test-host` alias documented |

---

## 5. Migration strategy (staged)

### Stage 0 — Before 0.0.1 tag (allowed now, minimal risk)

**Goal:** Finish critical path; do not move directories yet.

| Action | Risk | Notes |
|--------|------|-------|
| Commit Makefile split (`run.mk`, `legacy-run.mk`) | Low | Already drafted in working tree; **without** ktm/futex hunks |
| Commit `setup/make/legacy-smokes.mk` + qa include | Low | Behind `IR0_LEGACY_SMOKE` |
| Fix `qa:` default to **not** include kill-sigterm | Low | One line |
| Triage/working tree into bisectable commits | Medium | Per prior triage map |
| Add `make help-daily` / trim `make help` | Low | Docs in Makefile only |

**Exit criteria:** `wait4_wnohang` audit PASS stable (≥3 consecutive); 562 → bounded dirty set.

### Stage 1 — Immediately after 0.0.1 tag (1–2 weeks)

**Goal:** Physical moves with symlinks; zero behavior change.

1. Create `qa/` skeleton; **move** `scripts/linux_abi` → `qa/linux_abi` (leave symlink).
2. Split `scripts/make/qa.mk` → `qa/make/audit.mk` + `qa/make/tier1.mk`.
3. Move `scripts/smoke_*.py` → `qa/smokes/scripts/`.
4. CI/Makefile: single `IR0_INCLUDE_QA=1 make qa` includes new paths.
5. Run full CTR matrix once.

**Exit criteria:** All existing gates green; paths deprecated but symlinked.

### Stage 2 — Post-0.0.1 cleanup (2–4 weeks)

**Goal:** Relocate legacy smokes; shrink `setup/pid1`.

1. Inventory `legacy-smokes.mk` → manifest YAML (smoke id, init source, ISO, tags).
2. Move `init_fase*.c` / `fase*.c` to `qa/smokes/legacy/sources/`.
3. Keep in `setup/pid1/`: `irinit.c`, `init_musl.c`, `init_minimal.c`, tier-1 production.
4. Move `ktm/` → `diagnostics/ktm/`; Kconfig `CONFIG_KTM` off in defconfig.
5. Move `kernel/test/` → `qa/ktest/` (update includes in kernel build).

**Exit criteria:** `setup/pid1/` < 20 source files; legacy manifest drives CI optional job.

### Stage 3 — 0.1.x architecture (deferred)

| Action | Wait for |
|--------|----------|
| `includes/ir0/*.c` → `kernel/lib/` or subsystem trees | ARCH sprint approved |
| `debug_bins/` → `userspace/debug_bins/` | Tier-1 rootfs stable |
| `setup/experiments/` doom/runit isolation | T2 roadmap decision |
| Auto-generate audit.mk from `contracts.json` | Contract registry stable |

---

## 6. Risks

| Risk | Impact | Mitigation |
|------|--------|------------|
| Break bisect during moves | High | Stage 1 symlinks; one move per commit; CTR after each |
| CI/docs hardcoded paths | Medium | Grep gate in `architecture_guard.py` for forbidden paths |
| Developer muscle memory (`make smoke-fase*`) | Low | Wrappers + 1-release deprecation notice in SETUP.md |
| Parallel oleada (562 files) + reorg | **High** | **Freeze reorg until B/G stable and triage landed** |
| IR0 vs kernel-maintainer path refs | Low | Update INDEX `ir0.code` paths in acquired rules only |

---

## 7. What waits until 0.0.1 vs 0.1

| Item | Before 0.0.1 | After 0.0.1 (0.1) |
|------|--------------|-------------------|
| Commits B, G, wait audit stable | **Yes** | — |
| Makefile daily/legacy/qa split commit | **Yes** (stage 0) | — |
| Process lifecycle bundle | **No** until wait stable | Continue |
| Move `qa/linux_abi/` | No | Stage 1 |
| Move legacy fase smokes | No | Stage 2 |
| ktm → diagnostics | No | Stage 2 |
| debug_bins rename | No | Stage 3 |
| includes/ir0 `.c` migration | No | Stage 3 |
| Process lifecycle VERIFIED | Target 0.0.1 | — |

---

## 8. Recommended order after B/G

1. **Stabilize `wait4_wnohang`** — audit PASS (fix or harness; **single-root commit H** if needed).
2. **Land stage 0 Makefile split** from working tree (no ktm).
3. **Triage remaining 562 files** into bisectable commits (per prior map).
4. **Tag 0.0.1** when Capability Board rows agreed for release scope.
5. **Execute stage 1** of this plan (this document).
6. **Reopen process lifecycle bundle** only after step 1 is stable ≥3 runs.

---

## 9. Success metrics

| Metric | Current | Target (post stage 2) |
|--------|---------|------------------------|
| Root `Makefile` lines | ~2900 committed / ~1656 WIP | < 400 |
| `qa.mk` lines | 1564 | < 300 (includes only) |
| `setup/pid1/` C sources | ~110 | < 20 production + manifest |
| Daily `make help` targets | mixed with fase* | ≤ 15 |
| Legacy smokes discoverability | grep Makefile | `qa/smokes/legacy/manifest.yaml` |
| Release gate clarity | scattered | `IR0_INCLUDE_QA=1 make release-0.0.1` one entry |

---

## 10. References

- Capability Board: `Documentation/releases/IR0_CAPABILITY_BOARD.md`
- Contracts: `scripts/linux_abi/contracts.json`
- Prior triage (conversation): commits F, B, G map; park ktm/bulk
- kernel-maintainer: `EVOLUTION.md` — document acquired rules **after** wait stable
