#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Drop #include lines that no build of a file needs.

For each #include in each FILE.c, blank the line, rebuild the object for
x86_64 and arm64, and keep the removal only when both debug-stripped objects
stay byte-identical and no new warning appears. Lines are blanked (not
deleted) while probing so __LINE__ does not move; the surviving include
block is compacted at the end.

A header that declares a function this file defines is always kept: an
identical object does not prove it unneeded, because without it the compiler
no longer checks the definition against its prototype.

<config.h> is force-included by the Makefile in every kernel build (a tree
without .config builds setup/defconfig), so an explicit include of it is
always redundant.

Run from the repository root of a tree that is not building anything else:

    python3 scripts/include_prune.py mm/paging_uaccess.c kernel/syscalls/mm_vma.c
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
from architecture_guard import _header_prototypes  # noqa: E402

# Kernel -I order (Makefile CFLAGS); "x.h" also searches the file's directory.
INCLUDE_DIRS = (
    ROOT, ROOT / "includes", ROOT / "includes/uapi", ROOT / "ktm/include",
    ROOT / "includes/ir0", ROOT / "mm", ROOT / "arch/common",
    ROOT / "arch/x86-64/include", ROOT / "arch/arm64/include",
    ROOT / "arch/arm64/sources",
)
INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')
ISAS = (
    ("x86_64", {}, "strip"),
    ("arm64", {"ARCH": "arm64"}, "aarch64-linux-gnu-strip"),
)


def object_signature(obj: Path, isa_env: dict[str, str], strip: str) -> str | None:
    """Build @obj for one ISA; return the stripped hash, or None on failure."""
    obj.unlink(missing_ok=True)
    env = dict(os.environ, **isa_env)
    proc = subprocess.run(
        ["make", "-s", str(obj.relative_to(ROOT))],
        cwd=ROOT, env=env, capture_output=True, text=True, timeout=600,
    )
    if proc.returncode != 0 or not obj.is_file():
        return None
    if "warning:" in proc.stdout + proc.stderr:
        return None
    with tempfile.NamedTemporaryFile(suffix=".o") as tmp:
        if subprocess.run([strip, "-g", "-o", tmp.name, str(obj)],
                          capture_output=True).returncode != 0:
            return None
        return hashlib.sha256(Path(tmp.name).read_bytes()).hexdigest()


def signatures(obj: Path) -> tuple[str | None, ...]:
    return tuple(object_signature(obj, env, strip) for _, env, strip in ISAS)


def compact_include_block(lines: list[str]) -> list[str]:
    """Remove blank lines left inside and right after the include block."""
    idx = [i for i, line in enumerate(lines) if INCLUDE_RE.match(line)]
    if not idx:
        return lines
    first, last = idx[0], idx[-1]
    block = [line for line in lines[first:last + 1] if line.strip()]
    head = lines[:first]
    while len(head) >= 2 and not head[-1].strip() and not head[-2].strip():
        head.pop()
    tail = lines[last + 1:]
    while len(tail) >= 2 and not tail[0].strip() and not tail[1].strip():
        tail.pop(0)
    return head + block + tail


def resolve_header(source: Path, name: str, quoted: bool) -> Path | None:
    dirs = ((source.parent,) if quoted else ()) + INCLUDE_DIRS
    for base in dirs:
        candidate = base / name
        if candidate.is_file():
            return candidate
    return None


def declares_own_definition(source: Path, text: str, line: str) -> bool:
    """True when the included header declares a function @source defines."""
    match = INCLUDE_RE.match(line)
    header = resolve_header(source, match.group(1), '"' in line.split("include", 1)[1][:3])
    if header is None:
        return False
    for name in _header_prototypes(header.read_text(errors="replace")):
        if re.search(r"^(?!static\b)[A-Za-z_][^;{}]*\b" + re.escape(name)
                     + r"\s*\([^;{}]*\)\s*\{", text, re.M):
            return True
    return False


def prune(path: Path) -> list[str]:
    obj = path.with_suffix(".o")
    original = path.read_text()
    lines = original.split("\n")
    base = signatures(obj)
    if None in base:
        raise SystemExit(f"{path}: does not build cleanly on both ISAs")
    dropped: list[str] = []
    try:
        for i, line in enumerate(list(lines)):
            match = INCLUDE_RE.match(line)
            if not match:
                continue
            if declares_own_definition(path, original, line):
                continue
            lines[i] = ""
            path.write_text("\n".join(lines))
            if signatures(obj) == base:
                dropped.append(line.strip())
            else:
                lines[i] = line
                path.write_text("\n".join(lines))
        path.write_text("\n".join(compact_include_block(lines)))
    except BaseException:
        path.write_text(original)
        raise
    finally:
        obj.unlink(missing_ok=True)
    return dropped


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("files", nargs="+", type=Path)
    args = parser.parse_args()
    for rel in args.files:
        path = (ROOT / rel).resolve()
        if path.suffix != ".c":
            print(f"{rel}: skipped (only .c files)", file=sys.stderr)
            continue
        dropped = prune(path)
        print(f"{rel}: dropped {len(dropped)}")
        for line in dropped:
            print(f"  - {line}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
