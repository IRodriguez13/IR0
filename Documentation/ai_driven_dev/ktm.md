# KTM — Kernel Test / Trace Module

> **Last verified:** 2026-07-10  
> **Source of truth:** `includes/ir0/ktm/*`, `ktm/`, `make ktm-run`, `make ktm-userdev-run`,  
> land commit `f6c71e5`, [`KTM_FASE_PARITY.md`](../KTM_FASE_PARITY.md)

KTM is IR0's **sole kernel-side test and diagnostic source of truth**: typed events,
checkpoints, snapshots/probes, scenarios, `/dev/ktm` control plane, and host runners.
Legacy kernel `[FASE` serial is **retired** (arch-guard enforced).

**Agent policy:** prefer KTM scenarios / `libktm-user` / `KTM_CHECKPOINT` over new
serial tags. See the parity map before claiming a FASE oleada is “covered”.

---

## Facade

| Layer | Path |
|-------|------|
| Public API | `#include <ir0/ktm/ktm.h>` (+ `uapi.h` for userspace) |
| Implementation | `ktm/*.c`, `ktm/scenarios/` |
| Host tooling | `scripts/ktm_runner.py`, `scripts/ktm_userdev_runner.py` |
| Userspace pilot | `userspace/libktm/` (`ktm_fork_wait_case`) |

---

## Tiers (Kconfig)

| Tier | Kconfig | Role |
|------|---------|------|
| Core | `CONFIG_KTM` | checkpoints, panic class |
| Events | `CONFIG_KTM_EVENTS` | typed ring + transport `KTM\|…` |
| Flight | `CONFIG_KTM_FLIGHT` | panic dump of ring |
| Test | `CONFIG_KTM_TEST` | boot scenarios |
| Userdev | `CONFIG_KTM_USERDEV` | `/dev/ktm` |
| Fault | `CONFIG_KTM_FAULT` | stub (off in prod) |

---

## Gates

```bash
make -s ktm-run              # boot suite: lifecycle, pipe, cow_fork, exec, fork_rollback
make -s ktm-userdev-run      # fork_wait_signal via /dev/ktm
make -s arch-guard           # forbids [FASE in kernel trees
```

**`mm.cow_fork` scenario:** process/frame bookkeeping after fork (bounded frame growth).
Deep COW data-plane (FASE40 A–F) remains `make smoke-mm-cow-lazy` — see
[`mandocs/en/mm.md`](../mandocs/en/mm.md).

FASE oleada → KTM coverage: **[`KTM_FASE_PARITY.md`](../KTM_FASE_PARITY.md)**.

---

## Legacy notes (flight recorder)

Older flight macros remain in `ktm/include/ktm.h` (`KTM_FLIGHT`, …). New code should
use `ktm_event_emit4` / `KTM_CHECKPOINT` / scenarios.
