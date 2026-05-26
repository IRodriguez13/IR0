#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Sync IR0 AI-assisted development rules between the tracked tree and local IDE config.

Tracked canonical copy: Documentation/ai_driven_dev/
Local Cursor config (gitignored): .cursor/rules/*.mdc and .cursor/skills/

Usage:
  python3 scripts/sync_ai_dev_rules.py export   # .cursor -> Documentation/ai_driven_dev
  python3 scripts/sync_ai_dev_rules.py install  # Documentation/ai_driven_dev -> .cursor
"""

from __future__ import annotations

import argparse
import re
import shutil
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DOC_ROOT = ROOT / "Documentation" / "ai_driven_dev"
RULES_SRC = ROOT / ".cursor" / "rules"
SKILL_SRC = ROOT / ".cursor" / "skills" / "ctr" / "SKILL.md"
RULES_DST = DOC_ROOT / "rules"
SKILL_DST = DOC_ROOT / "skills" / "ctr" / "SKILL.md"

FRONTMATTER_RE = re.compile(r"^---\s*\n.*?\n---\s*\n", re.DOTALL)
META_LINE_RE = re.compile(r"^(\w+):\s*(.+)$")


def parse_frontmatter(text: str) -> tuple[dict[str, str], str]:
    match = FRONTMATTER_RE.match(text)
    if not match:
        return {}, text
    meta: dict[str, str] = {}
    block = match.group(0)
    for line in block.splitlines():
        m = META_LINE_RE.match(line.strip())
        if m:
            meta[m.group(1)] = m.group(2).strip()
    body = text[match.end():]
    return meta, body.lstrip("\n")


def build_frontmatter(meta: dict[str, str]) -> str:
    if not meta:
        return ""
    lines = ["---"]
    for key in ("description", "globs", "alwaysApply"):
        if key in meta:
            lines.append(f"{key}: {meta[key]}")
    lines.append("---")
    return "\n".join(lines) + "\n\n"


def rewrite_paths_to_doc(text: str) -> str:
    repl = [
        (r"\.cursor/skills/ctr/SKILL\.md", "Documentation/ai_driven_dev/skills/ctr/SKILL.md"),
        (r"\.cursor/rules/([A-Za-z0-9_-]+)\.mdc", r"Documentation/ai_driven_dev/rules/\1.md"),
        (r"`([A-Za-z0-9_-]+)\.mdc`", r"`\1.md`"),
        (r"under `/esp`", "under `Documentation/esp/`"),
        (r"Madurez orientativa y reglas de investigación web \+ multiagente en `\.cursor/rules/`:",
         "Madurez orientativa y reglas de investigación web + multiagente en "
         "`Documentation/ai_driven_dev/rules/`:"),
    ]
    out = text
    for pat, sub in repl:
        out = re.sub(pat, sub, out)
    return out


def rewrite_paths_to_cursor(text: str) -> str:
    repl = [
        (r"Documentation/ai_driven_dev/skills/ctr/SKILL\.md", ".cursor/skills/ctr/SKILL.md"),
        (r"Documentation/ai_driven_dev/rules/([A-Za-z0-9_-]+)\.md", r".cursor/rules/\1.mdc"),
        (r"`([A-Za-z0-9_-]+)\.md`", r"`\1.mdc`"),
    ]
    out = text
    for pat, sub in repl:
        out = re.sub(pat, sub, out)
    return out


def export_rules() -> int:
    if not RULES_SRC.is_dir():
        print(f"export: skip rules (missing {RULES_SRC})", file=sys.stderr)
        return 0
    RULES_DST.mkdir(parents=True, exist_ok=True)
    count = 0
    for src in sorted(RULES_SRC.glob("*.mdc")):
        raw = src.read_text(encoding="utf-8")
        meta, body = parse_frontmatter(raw)
        body = rewrite_paths_to_doc(body)
        dst = RULES_DST / f"{src.stem}.md"
        header = (
            f"<!-- IR0 AI dev rule: {src.stem} -->\n"
            f"<!-- alwaysApply: {meta.get('alwaysApply', 'false')} -->\n"
            f"<!-- description: {meta.get('description', '')} -->\n\n"
        )
        dst.write_text(header + body, encoding="utf-8")
        count += 1
    return count


def export_skill() -> bool:
    if not SKILL_SRC.is_file():
        print(f"export: skip skill (missing {SKILL_SRC})", file=sys.stderr)
        return False
    SKILL_DST.parent.mkdir(parents=True, exist_ok=True)
    text = rewrite_paths_to_doc(SKILL_SRC.read_text(encoding="utf-8"))
    SKILL_DST.write_text(text, encoding="utf-8")
    return True


def install_rules() -> int:
    if not RULES_DST.is_dir():
        print(f"install: missing {RULES_DST}", file=sys.stderr)
        return 1
    RULES_SRC.mkdir(parents=True, exist_ok=True)
    count = 0
    for src in sorted(RULES_DST.glob("*.md")):
        src_text = src.read_text(encoding="utf-8")
        raw = re.sub(r"^<!--.*?-->\n", "", src_text, flags=re.MULTILINE)
        always = "true" if "alwaysApply: true" in src_text else "false"
        desc_match = re.search(r"<!-- description: (.*?) -->", src_text)
        desc = desc_match.group(1) if desc_match else f"IR0 rule {src.stem}"
        globs_match = re.search(r"<!-- globs: (.*?) -->", src_text)
        body = rewrite_paths_to_cursor(raw)
        meta = {"description": desc, "alwaysApply": always}
        if globs_match:
            meta["globs"] = globs_match.group(1).strip()
        elif src.stem == "kernel-docs-language-policy":
            meta["globs"] = "**/*.md"
            meta["alwaysApply"] = "false"
        dst = RULES_SRC / f"{src.stem}.mdc"
        dst.write_text(build_frontmatter(meta) + body, encoding="utf-8")
        count += 1
    return count


def install_skill() -> bool:
    if not SKILL_DST.is_file():
        print(f"install: missing {SKILL_DST}", file=sys.stderr)
        return False
    SKILL_SRC.parent.mkdir(parents=True, exist_ok=True)
    text = rewrite_paths_to_cursor(SKILL_DST.read_text(encoding="utf-8"))
    SKILL_SRC.write_text(text, encoding="utf-8")
    return True


def cmd_export(_: argparse.Namespace) -> int:
    n = export_rules()
    skill = export_skill()
    print(f"export: {n} rules -> {RULES_DST.relative_to(ROOT)}")
    if skill:
        print(f"export: skill -> {SKILL_DST.relative_to(ROOT)}")
    return 0


def cmd_install(_: argparse.Namespace) -> int:
    n = install_rules()
    skill = install_skill()
    agents_src = DOC_ROOT / "AGENTS.md"
    agents_dst = ROOT / "AGENTS.md"
    if agents_src.is_file():
        shutil.copy2(agents_src, agents_dst)
        print(f"install: AGENTS.md -> {agents_dst.relative_to(ROOT)}")
    print(f"install: {n} rules -> {RULES_SRC.relative_to(ROOT)}")
    if skill:
        print(f"install: skill -> {SKILL_SRC.relative_to(ROOT)}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("export", help="Copy local .cursor rules into Documentation/ai_driven_dev")
    sub.add_parser("install", help="Install Documentation/ai_driven_dev rules into .cursor")
    args = parser.parse_args()
    if args.cmd == "export":
        return cmd_export(args)
    if args.cmd == "install":
        return cmd_install(args)
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
