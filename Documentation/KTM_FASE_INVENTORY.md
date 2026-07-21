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
| smoke-fase44-exec-drain | 44 | B→SUB | `ktm-userdev-exec-drain-virtfs-run` | 64× fork+exec+f41true + 9p |
| smoke-fase44-init-exit-drain | 44 | B→SUB | `ktm-userdev-init-exit-drain-virtfs-run` | PID1 `_exit` covered in userdev |
| smoke-fase50* / 51 | 50–51 | A | `ktm-run` exec/shell/open | Legacy bring-up |
| smoke-fase52-tcc / tcc-power | 52 | C | `smoke-tcc-power-halt` + KTM case | Product |
| smoke-fase53a / 53b | 53 | A/B→SUB | `vfs.devfs` + `ktm-userdev-posix-pseudofs-virtfs-run` | 53B COVERED (getdents + access) |
| smoke-fase54a/b/c | 54 | A/B→SUB | `graphics.fb` / `input.events0` + `ktm-userdev-input-det-virtfs-run` | 54C SUB |
| smoke-fase55* | 55 | C | `smoke-fase55d-doomgeneric` + KTM | Product WAD |
| (no smoke-fase57) | 57 | HOST | — | GUI/reintegration docs only |
| smoke-fase58* / smoke-busybox-manifest | 58 | C | ash/BUSY + KTM | BUSY-1/2 **Closed** |
| smoke-nic-reach | F8-1 | C | `ktm-userdev-nic-reach-virtfs-run` | L3 ping/ARP proof |
| smoke-tcp-guest | F8-2 | C | `ktm-userdev-tcp-guest-virtfs-run` | Guest→host wire 10.0.2.2:8889 |
| smoke-tcp-wire | F8-3 | C | `ktm-userdev-tcp-wire-virtfs-run` | Wire TCP send+recv 10.0.2.2:8888 |
| smoke-tcp-listen | F8 listen | C | `smoke-tcp-listen` | Host→guest listen/accept :7777 |
| smoke-f8-net | F8 battery | C | nic+guest+wire+listen | Honest MVP gate |

Deprecation policy: print warning on legacy target; do not delete for one oleada of grace.
