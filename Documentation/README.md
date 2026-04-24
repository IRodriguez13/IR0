# IR0 Kernel Documentation

This directory describes the current architecture and operational state of IR0.
The goal is to document implemented behavior first, then known gaps.

## Language Policy

- Primary technical documentation is maintained in English in `Documentation/`.
- Spanish translations are maintained in `Documentation/esp/`.
- Do not mix English and Spanish in the same technical document.

## Documentation Map

- `TOOLING.md`: build, menuconfig flow, validation targets, and runtime checks.
- `FILESYSTEM.md`: VFS layer, mounted filesystems, permissions path, and policy.
- `VIRTUAL_FILESYSTEMS.md`: `/proc`, `/dev`, `/sys`, and observable interfaces.
- `DRIVERS.md`: driver registry, bootstrap flow, and config-gated initialization.
- `INTERRUPTS.md`: IDT/PIC path, syscall entry, and exception behavior.
- `MEMORY.md`: PMM, allocator, paging model, and current limits.
- `PROCESSES.md`: process lifecycle, credentials, signals, and wait/reap behavior.
- `SCHEDULING.md`: scheduler selection and current policy implementations.
- `UNIX_DIFFERENCES.md`: compatibility boundaries and intentional divergences.

## Current Strengths

- Strong subsystem decoupling through facade headers and policy boundaries.
- Kconfig-driven build composition with reproducible profile switching.
- Driver bootstrap and registration flow centralized and observable at runtime.
- Broad pseudo-filesystem surface (`/proc`, `/dev`, `/sys`) for introspection.
- Stabilization workflow includes build matrix and runtime smoke validation.

## Current Weak Points

- Security model is still an MVP (minimal accounts and sudo path).
- Scheduler alternatives are present but not fully feature-rich yet.
- SMP and advanced preemption behavior are not the primary focus yet.
- Some interfaces remain kernel-hobby oriented rather than production hardened.
- Test depth is improving, but long-tail runtime scenarios need expansion.

## Translation Status

The Spanish mirror of this documentation lives under `Documentation/esp/` with
matching filenames.
