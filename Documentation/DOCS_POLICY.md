# Documentation Policy

IR0 documentation has three layers:

1. A concise README beside a critical subsystem for orientation and contracts.
2. Focused documents here for design, operation, debt, and historical context.
3. Bilingual `mandocs/` chapters for long-form subsystem teaching material.

## Subtree README contract

Keep a normal subsystem README around 40–120 lines and below 200 lines. It
should identify purpose, boundaries, entry points, invariants, build selection,
validation commands, and honest gaps. Link to canonical material instead of
copying it. A README is not a changelog, backlog, source listing, or tutorial.

Do not add placeholders merely to make every directory symmetrical. Add a file
when a developer or curious reader gains a stable map of a real boundary.

## Continuous maintenance

Any change to a facade, lifetime, ABI, provider selection, build graph, or
debugging workflow must review the nearest README and canonical document. A
code change may legitimately require no documentation edit, but stale claims
discovered during the work must be fixed or entered in the backlog.

Primary technical prose is English. Spanish mirrors live in an `esp/` subtree.
Claims must match code and tests; staged providers and unsupported behavior are
named explicitly.

## Rollout

The first audit covers `arch`, `kernel`, `mm`, `sched`, `fs`, `drivers`,
`interrupt`, `net`, `security`, `ktm`, `includes/ir0`, and `scripts`. Existing
large indexes—especially `arch/README.md`—should be split into a short landing
page plus focused documents without losing useful history.
