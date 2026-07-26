#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Sync Unix-kernel Cursor rules between IR0 and ~/.cursor/rules/kernel-linux/.

Canonical home copy (all kernel/Linux dev rules for IR0-class projects):
  ~/.cursor/rules/kernel-linux/*.mdc

Project consults home via symlinks in IR0/.cursor/rules/ (gitignored).

Usage:
  python3 scripts/sync_kernel_linux_rules.py publish-home
  python3 scripts/sync_kernel_linux_rules.py install-project
  python3 scripts/sync_kernel_linux_rules.py status
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROJECT_RULES = ROOT / ".cursor" / "rules"
HOME_KERNEL = Path.home() / ".cursor" / "rules" / "kernel-linux"

# IR0 + portable kernel discipline (explicit — not auto-discovered).
IR0_KERNEL_RULE_NAMES = (
    "ir0-contract-iteration-states",
    "ir0-development-multiagent-format",
    "ir0-development-plan-mode",
    "ir0-mandocs-initiative",
    "ir0-optimization-arch-sprints",
    "ir0-post-impl-log-hygiene",
    "ir0-roadmap-research-multiagent",
    "ir0-script-memory-safety",
    "ir0-smoke-autokill",
    "ir0-tier-t0-os-functional",
    "ir0-tier-t1-userspace-posix",
    "ir0-tier-t2-graphics-fullscreen",
    "ir0-tier-t3-desktop-minimal",
    "ir0-userspace-monolith-debt",
    "kernel-architecture-rigor",
    "kernel-c-allman-style",
    "kernel-docs-language-policy",
    "kernel-error-handling",
    "kernel-locking-discipline",
    "kernel-memory-ordering",
    "kernel-resource-lifecycle",
    "kernel-userspace-abi",
    "linux-ground-truth-first",
    "oss-kernel-reference",
)

# Upstream-only workspace index — do not install into IR0.
UPSTREAM_ONLY = frozenset({"linux-upstream-workspace-index"})


def _discover_upstream_rules() -> list[str]:
    if not HOME_KERNEL.is_dir():
        return []
    names: list[str] = []
    for path in sorted(HOME_KERNEL.glob("linux-upstream-*.mdc")):
        stem = path.stem
        if stem in UPSTREAM_ONLY:
            continue
        names.append(stem)
    return names


def _all_install_names() -> list[str]:
    seen: set[str] = set()
    ordered: list[str] = []
    for name in (*IR0_KERNEL_RULE_NAMES, *_discover_upstream_rules()):
        if name in seen:
            continue
        seen.add(name)
        ordered.append(name)
    return ordered


def _src_for(name: str) -> Path | None:
    proj = PROJECT_RULES / f"{name}.mdc"
    if proj.is_file() and not proj.is_symlink():
        return proj
    home = HOME_KERNEL / f"{name}.mdc"
    if home.is_file():
        return home
    if proj.is_file():
        return proj
    return None


def _write_ir0_workspace_index() -> None:
    path = PROJECT_RULES / "ir0-workspace.mdc"
    path.write_text(
        """---
description: IR0 kernel workspace — tiers, smokes, upstream discipline
alwaysApply: true
---

# IR0 workspace

Kernel Unix-like **IR0**. Reglas canónicas en `~/.cursor/rules/kernel-linux/`.

Instalar/actualizar symlinks (IR0 + linux-upstream):

```bash
make kernel-linux-rules-install
# o: ./scripts/install-cursor-rules.sh
```

## Capas

| Capa | Reglas | Verificación |
|------|--------|--------------|
| IR0 tiers T0–T3 | `ir0-tier-t*` | `make smoke-*`, tags en serial |
| POSIX / userspace | `ir0-tier-t1`, `kernel-userspace-abi` | musl smokes, contratos ABI |
| Arquitectura | `kernel-architecture-rigor`, `kernel-c-allman-style` | `make arch-guard`, CTR gates |
| Linux ground truth | `linux-ground-truth-first` | `scripts/d1_13_linux_ground_truth.py` |
| Upstream discipline | `linux-upstream-*` | referencia Linux/BSD al diseñar syscalls, MM, drivers |

## Gates CTR (cierre de oleada)

```bash
make -s kernel-x64.bin
make -s arch-guard
make -s build-matrix-min
make -s -C tests/host run
```

**Nota:** estilo C en IR0 es **Allman** (`kernel-c-allman-style`); `linux-upstream-coding-style` aplica como referencia OSS, no para imitar tabs K&R en el fork.
""",
        encoding="utf-8",
    )


def _write_project_readme(count: int) -> None:
    pointer = PROJECT_RULES / "README.md"
    pointer.write_text(
        f"""# IR0 Cursor rules (project)

Kernel/Linux rules are **symlinks** to `~/.cursor/rules/kernel-linux/` ({count} files).

```bash
make kernel-linux-rules-install   # refresh symlinks from home (IR0 + linux-upstream)
make ai-dev-rules-install         # AGENTS.md + doc-backed rules
```

Includes all `linux-upstream-*.mdc` from home except the upstream-tree workspace index.
""",
        encoding="utf-8",
    )


def cmd_publish_home(_: argparse.Namespace) -> int:
    HOME_KERNEL.mkdir(parents=True, exist_ok=True)
    n = 0
    for name in _all_install_names():
        src = PROJECT_RULES / f"{name}.mdc"
        if src.is_symlink():
            src = HOME_KERNEL / f"{name}.mdc"
        if not src.is_file():
            print(f"  skip  {name}.mdc (missing)", file=sys.stderr)
            continue
        dst = HOME_KERNEL / f"{name}.mdc"
        if src.resolve() == dst.resolve():
            n += 1
            continue
        shutil.copy2(src, dst)
        n += 1
    readme = HOME_KERNEL / "README.md"
    if not readme.is_file():
        readme.write_text(
            "# kernel-linux — Cursor rules (IR0 / Unix kernel development)\n\n"
            "See IR0 `scripts/sync_kernel_linux_rules.py` and `make kernel-linux-rules-install`.\n",
            encoding="utf-8",
        )
    print(f"publish-home: {n} rules -> {HOME_KERNEL}")
    return 0


def cmd_install_project(_: argparse.Namespace) -> int:
    if not HOME_KERNEL.is_dir():
        print(f"install-project: missing {HOME_KERNEL} — populate home rules first", file=sys.stderr)
        return 1
    PROJECT_RULES.mkdir(parents=True, exist_ok=True)
    n = 0
    for name in _all_install_names():
        src = HOME_KERNEL / f"{name}.mdc"
        if not src.is_file():
            print(f"  skip  {name}.mdc (not in home)", file=sys.stderr)
            continue
        dst = PROJECT_RULES / f"{name}.mdc"
        if dst.is_symlink() and dst.resolve() == src.resolve():
            n += 1
            continue
        if dst.exists() or dst.is_symlink():
            dst.unlink()
        dst.symlink_to(src)
        n += 1
    _write_ir0_workspace_index()
    _write_project_readme(n)
    print(f"install-project: {n} symlinks + ir0-workspace.mdc -> {PROJECT_RULES.relative_to(ROOT)}")
    return 0


def cmd_status(_: argparse.Namespace) -> int:
    print(f"HOME    {HOME_KERNEL}")
    print(f"PROJECT {PROJECT_RULES}")
    for name in _all_install_names():
        home = HOME_KERNEL / f"{name}.mdc"
        proj = PROJECT_RULES / f"{name}.mdc"
        if proj.is_symlink():
            mark = "symlink" if proj.resolve() == home.resolve() else "symlink(other)"
        elif proj.is_file():
            mark = "copy"
        else:
            mark = "missing"
        home_mark = "ok" if home.is_file() else "missing"
        print(f"  {name:45s} home={home_mark:7s} project={mark}")
    ws = PROJECT_RULES / "ir0-workspace.mdc"
    print(f"  {'ir0-workspace':45s} home= n/a     project={'ok' if ws.is_file() else 'missing'}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("publish-home", help="Copy project/home rules into ~/.cursor/rules/kernel-linux/")
    sub.add_parser("install-project", help="Symlink IR0/.cursor/rules/ from home kernel-linux/")
    sub.add_parser("status", help="Show sync status")
    args = parser.parse_args()
    if args.cmd == "publish-home":
        return cmd_publish_home(args)
    if args.cmd == "install-project":
        return cmd_install_project(args)
    if args.cmd == "status":
        return cmd_status(args)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
