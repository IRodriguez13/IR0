---
name: ctr
description: Enforces strict IR0 kernel architecture rigor: subsystem decoupling through interfaces/facades, SPDX/file header hygiene, configuration/build-matrix validation, and mandatory compile/testing checks. Use when implementing/refactoring kernel code, drivers, syscalls, scheduler/filesystem changes, or reviewing architecture consistency.
---
# CTR - IR0 Architectural Rigor

## Goal

Keep IR0 architecture consistent and scalable while preserving build stability.

## Mandatory Workflow

Copy this checklist and keep it updated while working:

```text
CTR Progress
- [ ] Scope identified (subsystems/files impacted)
- [ ] Architectural constraints checked
- [ ] Interfaces/facades preserved or improved
- [ ] Headers/comments/SPDX hygiene checked
- [ ] Config/build matrix validated
- [ ] Arch guard checks passed
- [ ] Final compile/test status reported
```

## Architectural Rules

1. In `fs/*` and `kernel/syscalls.c`, do not add direct `#include <drivers/...>`.
2. Access hardware-facing capabilities through `includes/ir0/*` facades.
3. Prefer extending facade headers over leaking driver internals upward.
4. Keep scheduler/filesystem selection compatible with config-driven build.
5. New subsystem toggles must be wired across:
   - `setup/Kconfig`
   - `setup/defconfig`
   - `scripts/kconfig/menuconfig.py` presets/CLI behavior
   - `Makefile` object gating and `CFLAGS` defines

## File Hygiene Rules

For modified/new kernel C sources and headers:

1. Ensure SPDX line exists.
2. Keep module/file description header style consistent with IR0.
3. Avoid TODO placeholders in implementation.
4. Keep comments concise and Linux-style where needed.

## Required Validation Commands

Run in this order after substantial architecture changes:

```bash
make -s kernel-x64.bin
make -s build-matrix-min
make -s arch-guard
```

For broader config confidence:

```bash
make -s build-matrix-full
```

## Review Mode Checklist

When user asks for review:

1. Prioritize regressions, races, ABI breaks, config inconsistencies.
2. Confirm no forbidden includes were introduced.
3. Confirm new options are actually build-wired and testable.
4. Report findings ordered by severity, then residual risk.

## Deliverable Format

Always end with:

1. What changed architecturally.
2. What validations were executed.
3. Remaining risks and next hardening step.
