# IR0 test mocks / fixtures

> **Last verified:** 2026-07-20  
> **Source of truth:** headers under this tree; consumers in `tests/host/`,  
> `tests/ktm/`, and KTM fault inject sites in the kernel.

Canonical place for **reusable synthetic state** used by host tests and
documentation of KTM inject scenarios. Not linked into the production kernel.

## Layout

```text
tests/mocks/
  README.md                 This file
```

Add new domains as `tests/mocks/<subsystem>/…` (e.g. `mm/`, `vfs/`, `ipc/`).

## What belongs here

| Put here | Examples |
|----------|----------|
| Fixture constants / builders | Sample `cs`/`rip` pairs, fake `syscall_frame` seeds |
| Shared host-only helpers | Pure inline helpers included by several `test_*.c` |
| Documented inject shapes | Values mirroring a `KTM_FAULT_HIT(...)` site |

## What does **not** belong here

| Keep elsewhere | Why |
|----------------|-----|
| `tests/host/host_*_stub.c` | Link stubs so kernel `.c` files compile on the host |
| `tests/ktm/userdev/*.c` | QEMU pilots (real musl binaries) |
| `tests/ktm/scenarios/*.c` | In-kernel KTM scenarios |
| `includes/ir0/*` | Production facades (predicates live there; mocks only *use* them) |

## Usage (host)

`tests/host/Makefile` adds `-I$(KERNEL_ROOT)/tests/mocks` so tests can:

```c
#include <<subsystem>/<fixture>.h>
```

Prefer thin headers (inline / macros). Avoid pulling full `process_t` unless a
host-linked object already provides it.
