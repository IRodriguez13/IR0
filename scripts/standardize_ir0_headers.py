#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Normalize IR0 C/H file headers to the 2026 canonical block + SPDX + pragma once."""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COPYRIGHT_YEAR = "2026"

INCLUDE_DIRS = (
    "kernel",
    "fs",
    "mm",
    "net",
    "sched",
    "drivers",
    "arch",
    "includes",
    "ktm",
    "interrupt",
    "tests/host",
    "tests/kernel_memsafe",
)

INCLUDE_FILES = (
    "config.h",
    "setup/kconfig_build.c",
    "setup/kernel_config.h",
    "setup/subsystem_config.h",
)

INCLUDE_GLOBS = (
    "setup/pid1/*.c",
    "setup/pid1/*.h",
    "setup/doom/doomgeneric_ir0*.c",
)

EXCLUDE_PARTS = (
    "/setup/doom/upstream/",
    "/setup/third-party/",
    "/setup/pid1/fase52_staging/",
)

SPDX_LINE = "/* SPDX-License-Identifier: GPL-3.0-only */"

RE_FILE = re.compile(r"^\s*\*\s*File:\s*(.+)$", re.MULTILINE)
RE_DESC = re.compile(r"^\s*\*\s*Description:\s*(.+)$", re.MULTILINE)
RE_COPYRIGHT = re.compile(r"Copyright \(C\) (\d{4})\s+Iván Rodriguez")


def is_excluded(path: Path) -> bool:
    s = "/" + path.as_posix() + "/"
    return any(part in s for part in EXCLUDE_PARTS)


def collect_files() -> list[Path]:
    out: set[Path] = set()
    for d in INCLUDE_DIRS:
        base = ROOT / d
        if not base.is_dir():
            continue
        for p in base.rglob("*"):
            if p.suffix in (".c", ".h") and p.is_file() and not is_excluded(p):
                out.add(p)
    for rel in INCLUDE_FILES:
        p = ROOT / rel
        if p.is_file():
            out.add(p)
    for pat in INCLUDE_GLOBS:
        for p in ROOT.glob(pat):
            if p.is_file() and not is_excluded(p):
                out.add(p)
    return sorted(out)


def extract_metadata(text: str, basename: str) -> tuple[str, str]:
    file_name = basename
    description = ""

    m = RE_FILE.search(text)
    if m:
        file_name = m.group(1).strip()

    m = RE_DESC.search(text)
    if m:
        description = m.group(1).strip()

    for d in RE_DESC.findall(text):
        d = d.strip()
        if d and d != "IR0 kernel source/header file":
            description = d
            break

    if not description:
        m = re.search(
            r"/\*\*\s*\n\s*\*\s*IR0 Kernel — ([^\n*]+)",
            text,
        )
        if m:
            description = m.group(1).strip().rstrip(".")
        else:
            m = re.search(r"/\*\*\s*\n\s*\*\s*([^\n*]+)", text)
            if m and "Core system software" not in m.group(1):
                description = m.group(1).strip().rstrip(".")

    if not description:
        stem = Path(basename).stem.replace("_", " ")
        kind = "header" if basename.endswith(".h") else "source"
        description = f"IR0 kernel {kind} — {stem}"

    return file_name, description


def remove_include_guard(body: str) -> str:
    """Remove classic #ifndef/#define/#endif wrapper when converting to #pragma once."""
    lines = body.splitlines(keepends=True)
    i = 0
    while i < len(lines):
        stripped = lines[i].strip()
        if not stripped or stripped.startswith("//"):
            i += 1
            continue
        break

    if i + 1 >= len(lines):
        return body

    m0 = re.match(r"#ifndef\s+(\S+)", lines[i].strip())
    define_line = lines[i + 1].strip()
    m1 = re.match(r"#define\s+(\S+)(?:\s+(.+))?$", define_line)
    if not m0 or not m1 or m0.group(1) != m1.group(1):
        return body

    # Feature toggles use "#define FOO 0", not include guards.
    if m1.group(2) is not None and m1.group(2).strip():
        return body

    guard = m0.group(1)
    if not (guard.endswith("_H") or guard.endswith("_H_")):
        return body
    lines = lines[:i] + lines[i + 2 :]

    j = len(lines) - 1
    while j >= 0 and not lines[j].strip():
        j -= 1
    if j < 0:
        return "".join(lines)

    last = lines[j].strip()
    if last.startswith("#endif") and (guard in last or last == "#endif"):
        lines = lines[:j] + lines[j + 1 :]

    return "".join(lines)


def strip_leading_header(text: str, is_header: bool) -> str:
    s = text
    if s.startswith("\ufeff"):
        s = s[1:]

    changed = True
    while changed and s:
        changed = False
        s = s.lstrip("\n")
        if not s:
            break

        if s.startswith("// SPDX-License-Identifier:"):
            nl = s.find("\n")
            s = s[nl + 1 :] if nl >= 0 else ""
            changed = True
            continue

        if s.startswith("/* SPDX-License-Identifier:"):
            end = s.find("*/")
            if end >= 0:
                s = s[end + 2 :]
                changed = True
                continue

        if s.startswith("/**"):
            end = s.find("*/")
            if end >= 0:
                s = s[end + 2 :]
                changed = True
                continue

        if s.startswith("/*") and not s.startswith("/**"):
            end = s.find("*/")
            if end >= 0 and end < 512:
                s = s[end + 2 :]
                changed = True
                continue

        if is_header and s.startswith("#pragma once"):
            nl = s.find("\n")
            s = s[nl + 1 :] if nl >= 0 else ""
            changed = True
            continue

    s = s.lstrip("\n")

    if is_header:
        s = remove_include_guard(s)
        # Drop duplicate #pragma once left by older edits.
        while s.startswith("#pragma once"):
            nl = s.find("\n")
            s = s[nl + 1 :] if nl >= 0 else ""
            s = s.lstrip("\n")

    return s


def strip_trailing_endif(text: str) -> str:
    return text


def build_header(file_name: str, description: str, is_header: bool) -> str:
    block = (
        "/**\n"
        " * IR0 Kernel — Core system software\n"
        f" * Copyright (C) {COPYRIGHT_YEAR}  Iván Rodriguez\n"
        " *\n"
        " * This file is part of the IR0 Operating System.\n"
        " * Distributed under the terms of the GNU General Public License v3.0.\n"
        " * See the LICENSE file in the project root for full license information.\n"
        " *\n"
        f" * File: {file_name}\n"
        f" * Description: {description}\n"
        " */\n"
        "\n"
        f"{SPDX_LINE}\n"
    )
    if is_header:
        block += "\n#pragma once\n"
    block += "\n"
    return block


def already_canonical(text: str, file_name: str, description: str, is_header: bool) -> bool:
    if not text.startswith("/**\n * IR0 Kernel — Core system software"):
        return False
    end = text.find("*/")
    if end < 0:
        return False
    head = text[: end + 2]
    after = text[end + 2 :].lstrip("\n")
    if not after.startswith(SPDX_LINE):
        return False
    if text.count(SPDX_LINE) > 1:
        return False
    if "// SPDX-License-Identifier:" in text:
        return False
    after = after[len(SPDX_LINE) :].lstrip("\n")
    if is_header:
        if not after.startswith("#pragma once"):
            return False
        body = after[len("#pragma once") :].lstrip("\n")
        if re.match(r"#ifndef\s+\S+", body):
            return False
    m = RE_COPYRIGHT.search(head)
    if not m or m.group(1) != COPYRIGHT_YEAR:
        return False
    if f"File: {file_name}" not in head:
        return False
    if f"Description: {description}" not in head:
        return False
    return True


def process_file(path: Path, dry_run: bool) -> str:
    raw = path.read_text(encoding="utf-8", errors="replace")
    basename = path.name
    is_header = basename.endswith(".h")
    file_name, description = extract_metadata(raw, basename)
    body = strip_leading_header(raw, is_header)

    if already_canonical(raw, file_name, description, is_header):
        return "skip"

    new_text = build_header(file_name, description, is_header) + body
    if new_text == raw:
        return "skip"

    if not dry_run:
        path.write_text(new_text, encoding="utf-8")
    return "updated"


def main() -> int:
    dry_run = "--dry-run" in sys.argv
    stats = {"updated": 0, "skip": 0}
    for path in collect_files():
        result = process_file(path, dry_run)
        stats[result] = stats.get(result, 0) + 1
        if result == "updated" and dry_run:
            print(f"would update: {path.relative_to(ROOT)}")

    mode = "dry-run" if dry_run else "apply"
    print(
        f"standardize_ir0_headers [{mode}]: "
        f"updated={stats.get('updated', 0)} skip={stats.get('skip', 0)}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
