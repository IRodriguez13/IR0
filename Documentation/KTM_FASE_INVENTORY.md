# FASE smoke inventory → KTM class (historical)

> **Last verified:** 2026-09-22
> **Canonical names now:** [`HARNESS_MAP.md`](HARNESS_MAP.md)
> **Source of truth:** `setup/make/legacy-smokes.mk`, `make ktm-run` / `ktm-userdev-*`.

This file is a **redirect + historical class table**. Prefer semantic
targets (`smoke-ipc`, `smoke-doomgeneric`, `ktm-userdev-*`).
`smoke-fase*` names remain Make aliases.

Classification (plan FASE→KTM, kept for history):

| Class | Meaning |
|-------|---------|
| **A** | Kernel state — boot scenario can own the gate |
| **B** | Depth storm — needs userdev / QEMU case with real fork/exec |
| **C** | Product HOST — keep QEMU product gate; instrument with libktm-user |
| **SUB** | Already substituted by KTM gate as canonical (legacy optional) |

| Legacy target | Class | Canonical gate | Notes |
|---------------|-------|----------------|-------|
| `smoke-userspace-fase41-reclaim` → `smoke-reclaim` | B→SUB | `ktm-run` `process.reclaim_exit` + `ktm-userdev-fork-storm-run` | |
| `smoke-fase42-*` | B→SUB | `ktm-userdev-fork-storm-run` + `mm.page_tables` | |
| `smoke-fase44-fork-wait-drain` | B→SUB | `ktm-userdev-fork-storm-run` | |
| `smoke-fase44-exec-drain` | B→SUB | `ktm-userdev-exec-drain-virtfs-run` | |
| `smoke-fase44-init-exit-drain` | B→SUB | `ktm-userdev-init-exit-drain-virtfs-run` | |
| `smoke-fase50*` / 51 | A | `ktm-run` exec/shell/open | |
| `smoke-fase52-tcc` → `smoke-tcc` | C | `smoke-tcc-power-halt` + KTM | |
| `smoke-fase53a` / 53b | A/B→SUB | `vfs.devfs` + `ktm-userdev-posix-pseudofs-virtfs-run` | |
| `smoke-fase54a/b/c` | A/B→SUB | `graphics.fb` / `ktm-userdev-input-det-virtfs-run` | |
| `smoke-fase55*` → `smoke-doomgeneric` | C | Doom IWAD + `KTM_DOOMGENERIC_OK` | |
| `smoke-fase58*` | C | ash / BusyBox manifest + KTM | |

Parity intent: [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md).
