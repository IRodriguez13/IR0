# IR0 Cursor rules (project)

Kernel/Linux rules are **symlinks** to `~/.cursor/rules/kernel-linux/` (50 files).

Local (repo-owned) alwaysApply rules (kernel-first ABI — mandatory for agents):

| Rule | Role |
|------|------|
| `ir0-userspace-first-linux-abi.mdc` | IR0 adapts to Linux/musl/BusyBox; fix kernel before userspace belts |
| `ir0-no-userspace-patches.mdc` | Hard stop on port patches for ABI gaps; smokes + contracts as proof |

Also: `ir0-api-public-private.mdc` (facades / anti-coupling / `switch_to`).

```bash
make kernel-linux-rules-install   # refresh symlinks from home (IR0 + linux-upstream)
make ai-dev-rules-install         # AGENTS.md + doc-backed rules
```

Includes all `linux-upstream-*.mdc` from home except the upstream-tree workspace index.

**Branch policy:** integrate on `dev` only (then `master`/`stable`); no long-lived `feat/*` on upstream remotes.
