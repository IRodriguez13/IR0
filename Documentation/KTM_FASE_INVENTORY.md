# FASE smoke inventory → KTM class

> **Last verified:** 2026-07-12  
> **Source of truth:** `setup/make/legacy-smokes.mk` (`IR0_LEGACY_SMOKE=1`),  
> [`KTM_FASE_PARITY.md`](KTM_FASE_PARITY.md), `make ktm-run` / `ktm-userdev-*`.

Classification for migration (plan FASE→KTM ≥ exigente):

| Class | Meaning |
|-------|---------|
| **A** | Kernel state — boot scenario can own the gate |
| **B** | Depth storm — needs userdev / QEMU case with real fork/exec |
| **C** | Product HOST — keep QEMU product gate; instrument with libktm-user |
| **SUB** | Already substituted by KTM gate as canonical (legacy optional) |

| Legacy target (abbrev) | FASE | Class | Canonical gate (2026-07-12) | Notes |
|------------------------|------|-------|-----------------------------|-------|
| smoke-userspace-fase41-reclaim | 41 | B→SUB | `ktm-run` `process.reclaim_exit` + `ktm-userdev-fork-storm-run` | Storm depth in userdev |
| smoke-fase42-* (fork/pt/exec) | 42 | B→SUB | `ktm-userdev-fork-storm-run` + `mm.page_tables` | PT counters still boot |
| smoke-fase44-fork-wait-drain | 44 | B→SUB | `ktm-userdev-fork-storm-run` (64+32 + KTM) | Legacy 512 optional |
| smoke-fase44-exec/init-exit | 44 | B | still HOST depth | Next oleada |
| smoke-fase50* / 51 | 50–51 | A | `ktm-run` exec/shell/open | Legacy bring-up |
| smoke-fase52-tcc / tcc-power | 52 | C | `smoke-tcc-power-halt` + KTM case | Product |
| smoke-fase53* | 53 | A/C | `vfs.devfs` + HOST pseudofs | Mixed |
| smoke-fase54* | 54 | A/C | `graphics.fb` / `input.events0` | 54C HOST |
| smoke-fase55* | 55 | C | `smoke-fase55d-doomgeneric` + KTM | Product WAD |
| smoke-fase58* | 58 | C | ash/BUSY + KTM when available | BUSY-1/2 ship |

Deprecation policy: print warning on legacy target; do not delete for one oleada of grace.
