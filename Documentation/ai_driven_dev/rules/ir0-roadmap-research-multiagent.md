<!-- IR0 AI dev rule: ir0-roadmap-research-multiagent -->
<!-- alwaysApply: true -->
<!-- description: IR0 maturity roadmap — web research + multi-agent execution for userspace/graphics/desktop tiers -->

# IR0 Roadmap — Research & Multi-Agent

## Maturity ladder (do not overclaim)

| Tier | Target | ~Progress | Primary paths |
|------|--------|-----------|---------------|
| T0 | OS funcional + debug_bins | ~80% | `debug_bins/`, `kernel/syscalls.c`, `fs/*`, contracts |
| T1 | Userspace POSIX mínimo (init+musl) | ~40% | `kernel/elf_loader.c`, `kernel/process.c`, `setup/pid1/`, `includes/ir0/bits/` |
| T2 | Cliente gráfico fullscreen (Doom-class) | ~50% | `drivers/video/`, `/dev/fb0`, `/dev/events0`, `sys_mmap` |
| T3 | Escritorio minimalista (WM+panel) | ~15–20% | Out of kernel slice unless explicitly scoped |

Advance **one tier at a time** with vertical slices. Do not implement T3 WM/compositor inside the kernel tree.

## Mandatory web research (real sources)

Before specifying or coding ABI/protocol behavior:

1. **WebSearch** or **WebFetch** at least one primary source (Linux `man7`, musl/Linux ABI, OSDev, kernel.org uapi, Intel/UEFI docs for x86-64).
2. **Verify in repo** with `Grep`/`Read` — README claims (e.g. “TCP completo”) are not specs until code exists.
3. **Document source** in PR/commit body or task summary (URL or doc section), not invented constants.

Prefer: [Linux syscalls x86-64](https://filippo.io/linux-syscall64/), musl `arch/x86_64/syscall_arch.h`, Linux `input.h` / `fb.h` uapi, OSDev VBE/UHCI.

## Multi-agent workflow

For orchestration, oleada reports, and CTR gates, see **`ir0-development-multiagent-format.md`**.

For Plan mode (explore → AskQuestion → CreatePlan → implement), see **`ir0-development-plan-mode.md`**.

Quick reference when spanning ≥2 subsystems:

| Agent | Role | Deliverable |
|-------|------|-------------|
| explore | Web + codebase gap analysis | Prioritized P0/P1 list with file paths |
| generalPurpose | Implementation | Minimal diff, facades preserved |
| generalPurpose | Tests + CTR validation | `make kernel-x64.bin`, `arch-guard`, `build-matrix-min`, `tests/host` |

Launch **2–3 agents concurrently** when workstreams are independent (e.g. syscalls vs devfs vs host tests).

Merge order: config wiring → facades/API → syscall/VFS → drivers → tests → docs.

## CTR gates (every merge)

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host
```

## Anti-patterns

- Do not add `#include <drivers/...>` or `<kernel/...>` in `fs/`/`net/`/`mm/` (use `includes/ir0/*`).
- Do not mark a tier “done” without runnable proof (ktest, host test, or QEMU smoke).
- Do not expand scope to Wayland/X11/D-Bus without a separate T3 plan approved by the user.
