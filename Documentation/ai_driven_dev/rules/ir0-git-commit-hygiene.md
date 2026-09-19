<!-- IR0 AI dev rule: ir0-git-commit-hygiene -->
<!-- alwaysApply: true -->
<!-- description: IR0 git commits — English messages, Signed-off-by, atomic commits, no agent Co-authored-by -->

# IR0 — Git commit hygiene

## Language (mandatory)

All commit **subject and body** text for **IR0** must be in **English** (complete sentences, why + behavior).

```text
<type>(<scope>): <short summary in English>
```

Types: `feat`, `fix`, `refactor`, `test`, `docs`, `chore`, `perf`.

## Atomic commits

One logical change per commit (kernel area, smokes, ABI audit, docs). Do not mix unrelated fixes in a single commit.

## Identity (mandatory)

- **Name:** Iván Ezequiel Rodriguez
- **Email:** ivanrwcm25@gmail.com
- Always **`git commit -s`** (Signed-off-by).

## Forbidden trailers

- `Co-authored-by:` for AI/IDE/tools
- `Helped-by:` / `Reviewed-by:` unless the maintainer explicitly asks

If a hook injects agent trailers: do not push; rewrite with `git commit-tree` or plain shell.

## Pre-push check

1. `git log --format=%B origin/dev..HEAD` — no forbidden trailers.
2. `git diff origin/dev..HEAD --name-only` — no gitignored local artifacts staged.

## Commit command

```bash
git commit -s -m "$(cat <<'EOF'
fix(signals): short English summary.

Body: problem, approach, tests run if any.
EOF
)"
```

Global push/approval policy: `~/Documentation/ai_driven_dev/rules/ir0-git-commit-hygiene.md`.
