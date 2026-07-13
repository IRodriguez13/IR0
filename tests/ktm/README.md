# KTM tests (`tests/ktm/`)

> **Full documentation:** [`Documentation/KTM.md`](../../Documentation/KTM.md)
> (Spanish: [`Documentation/esp/KTM.md`](../../Documentation/esp/KTM.md))

Kernel Test Module (KTM) **test artifacts** live here. Runtime internals
(registry, transport, userdev driver, scenario runner) stay under `ktm/` at the
repo root.

## Layout

| Path | Role |
|------|------|
| `tests/ktm/scenarios/` | In-kernel scenarios linked into the kernel (`TEST_BEGIN` / `TEST_END` at boot) |
| `tests/ktm/userdev/` | musl static pilots injected as `/sbin/init` for QEMU smokes (`make ktm-userdev-*`) |
| `tests/ktm/lib/` | Shared userspace helper (`libktm_user.c`) over `/dev/ktm` ioctls |

Host-side KTM checks (panic inventory, sched contract) remain in `tests/host/`.

## Build

Userdev cases: `make build-ktm-<case>-case` or the matching `ktm-userdev-*-run`
/ `smoke-*` alias in the root `Makefile`.

In-kernel scenarios compile as `tests/ktm/scenarios/*.o` via the normal
`make kernel-x64.bin` path.
