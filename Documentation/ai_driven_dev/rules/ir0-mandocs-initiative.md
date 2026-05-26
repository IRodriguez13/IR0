<!-- IR0 AI dev rule: ir0-mandocs-initiative -->
<!-- alwaysApply: false -->
<!-- description: IR0 mandocs initiative — bilingual subsystem chapters, 10-section format, diagrams, build wiring -->
<!-- globs: Documentation/mandocs/**,scripts/build_mandocs.py -->

# IR0 Mandocs Initiative

Apply when creating or editing kernel internals documentation under
`Documentation/mandocs/` or registering chapters in `scripts/build_mandocs.py`.

Philosophy: IR0 must be studyable from inside the OS (MINIX/BSD/early UNIX style).
Document **implemented behavior** from the codebase; do not hide technical debt.

## Layout (mandatory)

| Path | Language |
|------|----------|
| `Documentation/mandocs/en/<slug>.md` | English |
| `Documentation/mandocs/esp/<slug>.md` | Spanish mirror (same structure, translated prose) |
| `Documentation/mandocs/diagrams/<slug>-*.mmd` | Mermaid sources |
| `Documentation/mandocs/TEMPLATE.md` | Chapter skeleton (do not drift from it) |
| `Documentation/mandocs/en/INDEX.md` | Status table (markdown navigation only — not a man page) |

Never mix English and Spanish in one chapter file.
Legacy flat docs (`Documentation/*.md`) stay during migration; mandocs **supersede** when marked `stable`.

## Diagrams (in chapters)

Every chapter section 7 must include an **ASCII diagram inline** (mandoc renders this).
Optional Mermaid in `Documentation/mandocs/diagrams/` is for export/git viewing only.
Do not rely on external-only diagrams; the chapter must stand alone in `man IR0-<slug>`.

## Metadata block (top of every chapter)

Required table before section 1:

| Field | Value |
|-------|-------|
| Version | semver-ish (0.1, 0.2, 1.0) |
| IR0 phase | T0 / T1 / T2 (primary tier) |
| Status | `experimental` / `stable` / `wip` |
| Depends on | chapter slugs or source paths |
| Man page | `IR0-<slug>` section 7 |
| Primary sources | real paths (`fs/`, `kernel/`, `arch/`, …) |

Bump **Version** when kernel releases change documented behavior.

## Ten required sections (in order)

1. **Overview** — scope, audience, out-of-scope
2. **Internal architecture** — structs, registries, ops; cite real files/symbols
3. **Data flow** — numbered steps; ASCII diagram if path has >4 hops
4. **Responsibilities** — subsystem vs caller duties
5. **Subsystem boundaries** — forbidden includes; `architecture_guard.py` tags
6. **Relations to other subsystems** — facades in `includes/ir0/*`
7. **Visual maps** — Mermaid in `diagrams/` + **ASCII fallback inline** (mandoc cannot render Mermaid)
8. **Important invariants** — correctness rules (CR3, longest-prefix mount, errno sign, …)
9. **Debugging tips** — serial tags, `/proc` nodes, ktest/host, common errno
10. **Future roadmap** — gaps, debt, tradeoffs; label **not implemented** explicitly

Aspirational content belongs **only** in section 10.

## Style

- Didactic and deep; no marketing or tier overclaiming.
- Explain architectural **why** and real **limitations**.
- Before ABI/protocol claims: verify Linux uapi / musl / OSDev + `Grep`/`Read` in tree.
- Diagrams: boxes and arrows only — no decorative noise.
- Do not copy entire legacy docs; link from INDEX or note supersession.

## Subsystem coverage (INDEX slugs)

boot, scheduler, memory, syscalls, vfs, filesystems, tty, drivers, process, userspace, multi-arch, net, interrupts, ipc, input, graphics, debug-bins, signals, security.

## Build wiring (required for new chapters)

1. Add `MandocChapter(...)` entry in `scripts/build_mandocs.py` (`MANDOC_CHAPTERS`).
2. `man_name` must match metadata (`IR0-vfs`, `IR0-boot`, …).
3. Update status row in both `mandocs/en/INDEX.md` and `mandocs/esp/INDEX.md`.
4. Validate:

```bash
python3 scripts/build_mandocs.py --lang en --mandoc-only --no-install
python3 scripts/build_mandocs.py --lang es --mandoc-only --no-install
man -l build/mandoc/en/IR0-<slug>.7
```

## Release discipline

On kernel releases touching a documented subsystem: update version, invariants, diagrams, section 10, and INDEX status.

## Anti-patterns

- Marking `stable` without grep-verified code alignment.
- Mermaid-only diagrams with no ASCII fallback in the chapter.
- English chapter without Spanish mirror (or vice versa for new work).
- Registering man page without `MANDOC_CHAPTERS` entry.
- Documenting README claims not present in source.
- Creating unrelated markdown outside `Documentation/mandocs/` when the user asked for mandocs only.

## Related rules

- `kernel-docs-language-policy.md` — general doc language (mandocs use `mandocs/esp/`, not `Documentation/esp/`).
- `kernel-architecture-rigor.md` — facades and guard tags for section 5.
- `ir0-roadmap-research-multiagent.md` — tier honesty for metadata **IR0 phase**.
