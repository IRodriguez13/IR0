# IR0 Kernel Documentation

This directory describes the current architecture and operational state of IR0.
The goal is to document implemented behavior first, then known gaps.

**Bootstrap and QEMU usage:** see [SETUP.md](../SETUP.md) at the repository root.

## Language Policy

- Primary technical documentation is maintained in English in `Documentation/`.
- Spanish translations are maintained in `Documentation/esp/`.
- Do not mix English and Spanish in the same technical document.

## Documentation Map

- `MAKEFILE.md`: Makefile as central orchestrator — config flow, target taxonomy, wiring rules.
- `TOOLING.md`: build, menuconfig flow, validation targets, and runtime checks.
- `ai_driven_dev/`: tracked AI agent rules, skills, and install/sync workflow (see below).
- `DECOUPLING.md`: subsystem boundaries, façade map, portable arch conventions, symbol digest.
- `FILESYSTEM.md`: VFS layer, mounted filesystems, permissions path, and policy.
- `VIRTUAL_FILESYSTEMS.md`: `/proc`, `/dev`, `/sys`, and observable interfaces.
- `PSEUDO_FS_HEART.md`: registry + `/heart` facade + ARCH-3 fd_table lifecycle.
- `STABLE.md`: release 0.0.1 checklist; merge→`master` blockers = TinyCC + Doom T2.
- `BACKLOG_REMAINING.md`: honest post-0.0.1 open work (storage, POSIX, residual).
- `KTM.md`: **canonical KTM guide** — internals, **klog layers**, kernel API, `/dev/ktm`, gates (Spanish: `esp/KTM.md`).
- `KTM_FASE_PARITY.md`: FASE oleada → KTM analogue map (COVERED/PARTIAL/GAP/SUB).
- `KTM_FASE_INVENTORY.md`: legacy `smoke-fase*` class A/B/C and canonical KTM gates.
- `CHANGELOG.md`: iteration notes (Unreleased + 0.0.1).
- `ai_driven_dev/ktm.md`: short agent-facing KTM index (points to `KTM.md`).
- `ai_driven_dev/rules/ir0-version-stamp.mdc`: lockstep `version.h` / Makefile with upstream tags.
- `releases/`: ABI board, syscall/pseudo map, 0.0.1 scope and write-path plan.
- `DRIVERS.md`: driver registry, bootstrap flow, config-gated initialization, **SB16 QEMU smoke**.
- `INTERRUPTS.md`: IDT/PIC path, syscall entry, and exception behavior.
- `MEMORY.md`: PMM, allocator, paging, real fork COW + lazy alloc limits.
- `PROCESSES.md`: process lifecycle, credentials, signals, and wait/reap behavior.
- `SCHEDULING.md`: scheduler selection, blocked poll/pause yield, Class B.
- `UNIX_DIFFERENCES.md`: compatibility boundaries and intentional divergences.
- `mandocs/`: **internals initiative** — bilingual subsystem chapters, diagrams, `man IR0-vfs` targets (see `mandocs/en/INDEX.md`; MM COW in `mandocs/en/mm.md`; **boot banner-first** in `mandocs/en/boot.md`).
- `fase58e-ash-interactive-console.md`: interactive BusyBox ash on `/dev/console`
  (QEMU GTK), keyboard poll + TTY echo path, build/run and serial tags.
## AI-assisted development

Rules for coding agents live in **`Documentation/ai_driven_dev/`** (tracked in git).
Local Cursor config under `.cursor/` is gitignored — install with:

```bash
make ai-dev-rules-install
```

## Unified manual pages

Generate and install kernel manuals (interactive — no extra flags):

```bash
make mandocs-en    # English: legacy chapters + mandocs internals (IR0-vfs, …)
make mandocs-es    # Spanish
man IR0-krnl       # legacy unified manual
man IR0-vfs        # subsystem chapter (mandocs initiative)
man IR0-net        # example: networking stack chapter
make mandocs-uninstall
```

**Mandocs initiative** (code-faithful subsystem chapters):

- English: `Documentation/mandocs/en/`
- Spanish: `Documentation/mandocs/esp/`
- Template: `Documentation/mandocs/TEMPLATE.md`
- Diagrams (Mermaid): `Documentation/mandocs/diagrams/`
- Index and status: `Documentation/mandocs/en/INDEX.md`

Default install uses `~/.local` (no sudo). System-wide: `sudo MANDOC_PREFIX=/usr/local make mandocs-en`.

Per-chapter pages: `build/mandoc/<lang>/IR0-krnl-<subsystem>.7` (legacy) and
`build/mandoc/<lang>/IR0-<subsystem>.7` (mandocs).

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
