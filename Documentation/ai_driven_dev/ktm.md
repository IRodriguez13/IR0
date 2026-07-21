# KTM — Kernel Test / Trace Module (agent index)

> **Last verified:** 2026-07-21  
> **Full guide:** [`../KTM.md`](../KTM.md) (internals + **klog layers** + kernel/user API)  
> **Source of truth:** `includes/ir0/ktm/*`, `ktm/` (incl. `klog.c`), `tests/ktm/`, `make ktm-run`,  
> `make ktm-userdev-*`, [`KTM_FASE_PARITY.md`](../KTM_FASE_PARITY.md)

KTM is IR0's **sole kernel-side test and diagnostic source of truth**: typed events,
checkpoints, snapshots/probes, scenarios, `/dev/ktm` control plane, and host runners.
Human serial logging is **klog** (`<ir0/ktm/klog.h>`), not raw `serial_print`.
Legacy kernel `[FASE` serial is **retired** (arch-guard enforced).

**Agent policy:** prefer KTM scenarios / `libktm-user` / `KTM_CHECKPOINT` / `klog_*`
over new ad-hoc serial dialects. See the parity map before claiming a FASE oleada is
“covered”. For how to author scenarios or userdev pilots, read **[`KTM.md`](../KTM.md)** first.

---

## Facade

| Layer | Path |
|-------|------|
| Public API (kernel) | `#include <ir0/ktm/ktm.h>` or `#include <ktm.h>` via `includes/ir0/ktm.h` |
| Human log | `#include <ir0/ktm/klog.h>` — `[ts] [LEVEL] [COMP]` |
| Private KTM helpers | `#include <ktm_internal.h>` (`ktm/include/`; **no** `../` / quoted paths) |
| Userspace UAPI | `#include <ir0/ktm/uapi.h>` + `/dev/ktm` (`tests/ktm/lib/`) |
| Implementation | `ktm/*.c` (internals; no scenarios) |
| In-kernel scenarios | `tests/ktm/scenarios/` |
| Host tooling | `scripts/ktm_runner.py`, `scripts/ktm_userdev_runner.py`, `scripts/smoke_autokill.py` |
| Userspace pilot | `tests/ktm/userdev/` (`ktm_fork_wait_case`, `ktm_cow_touch_case`, …) |
| Runit smoke tags | `setup/runit/ir0_smoke_tag.h` |

arch-guard rule `[ktm-include]` forbids relative/quoted includes into KTM from `ktm/` and `includes/ir0/ktm*`.
Makefile puts `-Iktm/include` **before** `-Iincludes/ir0` so `<ktm.h>` is not shadowed by the thin facade `includes/ir0/ktm.h`.

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
make -s ktm-run              # boot suite: lifecycle, pipe, cow_fork, exec, fork_rollback, …
make -s ktm-userdev-run      # fork_wait_signal via /dev/ktm
make -s ktm-userdev-fork-storm-run  # real fork depth (≥ wait_drain/reclaim boot)
make -s ktm-userdev-fork-storm-virtfs-run  # same + virtio-9p artifact on host
make -s ktm-userdev-exec-drain-virtfs-run  # FASE44 exec-drain + 9p (f41true inject)
make -s ktm-userdev-reap-drain-virtfs-run  # FASE44 reap-drain + 9p
make -s ktm-userdev-posix-pseudofs-virtfs-run  # FASE53B
make -s ktm-userdev-input-det-virtfs-run       # FASE54C
make -s smoke-nic-reach                        # F8-1 NIC + 9p
make -s smoke-hostshare-9p   # virtio-9p MVP (/mnt/host)
make -s arch-guard           # forbids [FASE in kernel trees
```

**Virtio for testing:** QEMU `-virtfs` / virtio-9p mounts a host directory at guest `/mnt/host`.
KTM cases may write result files there (`ktm_fork_storm.txt`, `ktm_exec_drain.txt`,
`ktm_reap_drain.txt`); the host runner checks the file without scraping serial alone.
Not virtiofs/FUSE.
**`mm.cow_fork` scenario:** process/frame bookkeeping after fork (bounded frame growth).
Deep COW data-plane (FASE40 A–F) remains `make smoke-mm-cow-lazy` — see
[`mandocs/en/mm.md`](../mandocs/en/mm.md).

FASE oleada → KTM coverage: **[`KTM_FASE_PARITY.md`](../KTM_FASE_PARITY.md)**;
target inventory: **[`KTM_FASE_INVENTORY.md`](../KTM_FASE_INVENTORY.md)**.

---

## Legacy notes (flight recorder)

Older flight macros remain in `ktm/include/ktm.h` (`KTM_FLIGHT`, …). New code should
use `ktm_event_emit4` / `KTM_CHECKPOINT` / scenarios.
