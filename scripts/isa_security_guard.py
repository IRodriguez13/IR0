#!/usr/bin/env python3
"""Reject privileged ISA operations escaping architecture-owned code.

This complements architecture_guard.py.  It deliberately checks executable C
and assembler text rather than identifiers used by diagnostics or facades.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
COMMON_TREES = ("kernel", "mm", "fs", "net", "ipc", "security", "lib", "include")
SOURCE_SUFFIXES = {".c", ".h", ".S", ".s"}

ARCH_INCLUDE = re.compile(r"^\s*#\s*include\s*[<\"](?:\.\./)*arch/", re.MULTILINE)
INLINE_ASM = re.compile(r"\b(?:__asm__|__asm|asm)\s*(?:volatile\s*)?\(")
PRIVILEGED_OPERAND = re.compile(
    r"\b(?:cr[0234]|invlpg|wrmsr|rdmsr|wbinvd|lidt|lgdt|ltr|"
    r"ttbr[01]_el1|vbar_el1|sctlr_el1|daifset|daifclr)\b",
    re.IGNORECASE,
)


def candidates() -> list[Path]:
    result: list[Path] = []
    for name in COMMON_TREES:
        base = ROOT / name
        if not base.exists():
            continue
        result.extend(
            path for path in base.rglob("*")
            if path.is_file() and path.suffix in SOURCE_SUFFIXES
        )
    return sorted(set(result))


def main() -> int:
    failures: list[str] = []
    for path in candidates():
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT)
        if ARCH_INCLUDE.search(text):
            failures.append(f"{rel}: direct arch/ include in common code")
        for match in INLINE_ASM.finditer(text):
            snippet = text[match.start():match.start() + 240]
            if PRIVILEGED_OPERAND.search(snippet):
                line = text.count("\n", 0, match.start()) + 1
                failures.append(f"{rel}:{line}: privileged inline assembly in common code")
    if failures:
        print("[isa-security-guard] FAILED", file=sys.stderr)
        for failure in failures:
            print(f"  - {failure}", file=sys.stderr)
        return 1
    print(f"[isa-security-guard] OK ({len(candidates())} common source files)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
