#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rewrite direct subsystem includes to <ir0/...> facades (architecture policy)."""

from __future__ import annotations

import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Order matters: net stack bundle before single net/* lines.
SUBST = [
    (
        re.compile(
            r"#include\s*<net/ip\.h>\s*\n"
            r"#include\s*<net/icmp\.h>\s*\n"
            r"#include\s*<net/dns\.h>\s*\n"
            r"#include\s*<net/arp\.h>\s*\n"
        ),
        "#include <ir0/net_stack.h>\n",
    ),
    (re.compile(r'#include\s*"scheduler_api\.h"'), "#include <ir0/scheduler_api.h>"),
    (re.compile(r'#include\s*"copy_user\.h"'), "#include <ir0/copy_user.h>"),
    (re.compile(r'#include\s*"signals\.h"'), "#include <ir0/signals.h>"),
    (re.compile(r"#include\s*<kernel/process\.h>"), "#include <ir0/process.h>"),
    (re.compile(r"#include\s*<kernel/elf_loader\.h>"), "#include <ir0/elf_loader.h>"),
    (re.compile(r"#include\s*<kernel/kernel\.h>"), "#include <ir0/kernel.h>"),
    (re.compile(r"#include\s*<mm/paging\.h>"), "#include <ir0/paging.h>"),
    (re.compile(r"#include\s*<mm/pmm\.h>"), "#include <ir0/pmm.h>"),
    (re.compile(r"#include\s*<mm/allocator\.h>"), "#include <ir0/allocator.h>"),
    (re.compile(r"#include\s*<fs/vfs\.h>"), "#include <ir0/vfs.h>"),
    (re.compile(r"#include\s*<sched/task\.h>"), "#include <ir0/task.h>"),
    (re.compile(r"#include\s*<sched/scheduler_api\.h>"), "#include <ir0/scheduler_api.h>"),
    (re.compile(r"#include\s*<net/ip\.h>"), "#include <ir0/net_stack.h>"),
    (re.compile(r"#include\s*<net/icmp\.h>"), "#include <ir0/net_stack.h>"),
    (re.compile(r"#include\s*<net/dns\.h>"), "#include <ir0/net_stack.h>"),
    (re.compile(r"#include\s*<net/arp\.h>"), "#include <ir0/net_stack.h>"),
]

SCAN = [
    ROOT / "kernel",
    ROOT / "drivers",
    ROOT / "interrupt",
    ROOT / "ktm",
    ROOT / "arch",
    ROOT / "sched" / "switch",
    ROOT / "includes" / "ir0",
]

FACADE_HDRS = {p.resolve() for p in (ROOT / "includes" / "ir0").glob("*.h")}

# console.c: backend facade already pulls drivers/video/console.h
DROP_LINES = {
    ROOT / "includes" / "ir0" / "console.c": re.compile(
        r"^\s*#include\s*<drivers/video/console\.h>\s*\n"
    ),
}


def dedupe_net_stack(text: str) -> str:
    lines = text.splitlines(keepends=True)
    out = []
    seen_net_stack = False
    for line in lines:
        if line.strip() == "#include <ir0/net_stack.h>":
            if seen_net_stack:
                continue
            seen_net_stack = True
        out.append(line)
    return "".join(out)


def process(path: Path) -> bool:
    if path.resolve() in FACADE_HDRS:
        return False
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        return False
    orig = text
    drop = DROP_LINES.get(path)
    if drop:
        text = drop.sub("", text)
    for pat, repl in SUBST:
        text = pat.sub(repl, text)
    text = dedupe_net_stack(text)
    if text != orig:
        path.write_text(text, encoding="utf-8")
        return True
    return False


def main() -> int:
    changed = 0
    for base in SCAN:
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix not in (".c", ".h"):
                continue
            if process(path):
                changed += 1
                print("updated:", path.relative_to(ROOT))
    print(f"fix_ir0_facade_includes: {changed} file(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
