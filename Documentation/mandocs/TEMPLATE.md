# IR0 Mandoc Chapter Template

Use this skeleton for every subsystem chapter under `Documentation/mandocs/en/`
with a matching file in `Documentation/mandocs/esp/`.

## Metadata block (required)

Place at the top of each chapter, before section 1:

```markdown
| Field | Value |
|-------|-------|
| Version | 0.1 |
| IR0 phase | T0 / T1 / T2 (pick primary tier) |
| Status | experimental / stable / wip |
| Depends on | comma-separated chapter slugs or source paths |
| Man page | IR0-<slug> (section 7) |
| Primary sources | paths under fs/, kernel/, arch/, … |
```

Update **Version** on every release that changes behavior documented here.
Bump minor for clarifications; bump major for semantic or ABI changes.

## Required sections

### 1. Overview

One to three paragraphs: what the subsystem does, who reads this chapter,
and what is intentionally out of scope.

### 2. Internal architecture

Components, structs, registries, ops tables. Name real files and symbols.
No marketing; state what exists today.

### 3. Data flow

Step-by-step path for the main operations (open, schedule, page fault, IRQ, …).
Prefer numbered lists; add an ASCII diagram when the path has more than four hops.

### 4. Responsibilities

Bullet list: what this subsystem **must** do vs what callers must do.

### 5. Subsystem boundaries

What this code must **not** do (forbidden includes, no direct hardware from `fs/`, …).
Reference `scripts/architecture_guard.py` tags when relevant.

### 6. Relations to other subsystems

Table or list: upstream callers, downstream backends, facades in `includes/ir0/`.

### 7. Visual maps

- **ASCII diagram inline in this section** (mandoc renders it; required).
- Optional Mermaid export: `Documentation/mandocs/diagrams/<slug>-*.mmd` (git/slides only).

Keep diagrams minimal: boxes and arrows only, no decorative styling.

### 8. Important invariants

Rules that must hold for correctness (locking, CR3 for copy_user, mount longest-prefix, …).

### 9. Debugging tips

Serial tags, `/proc` nodes, ktest/host tests, common failure modes and errno.

### 10. Future roadmap

Honest gaps, debt, and tradeoffs. Label **not implemented** clearly.

## Style rules

- English in `mandocs/en/`; Spanish in `mandocs/esp/` — no mixed languages in one file.
- Document **implemented** behavior first; aspirational items only in section 10.
- Cite primary sources (Linux uapi, musl ABI) when specifying syscall-visible semantics.
- Do not duplicate entire legacy `Documentation/*.md` files; link or supersede with a note in INDEX.

## Contributing

1. Copy this template into `mandocs/en/<slug>.md`.
2. Add the Spanish mirror under `mandocs/esp/<slug>.md`.
3. Register the slug in `scripts/build_mandocs.py` (`MANDOC_CHAPTERS`).
4. Add or update `Documentation/mandocs/diagrams/<slug>.mmd` if section 7 needs a map.
5. Run `make mandocs-en --no-install` (or `python3 scripts/build_mandocs.py --lang en --no-install`) and fix mandoc warnings.
6. On kernel releases that touch the subsystem, update version, invariants, and roadmap.
