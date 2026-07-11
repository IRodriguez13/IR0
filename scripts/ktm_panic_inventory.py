#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Audit kernel panic call sites for KTM-friendly panicex metadata.

Gate (--check):
  - includes/ir0/oops.h must define panic() as a macro (callsite file/line).
  - No void panic(const char ...) function in .c sources.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

SKIP_PARTS = (
    "setup/third-party",
    "/build/",
    "/.git/",
    "/iso/",
)

OOPS_H = ROOT / "includes/ir0/oops.h"
PANIC_FN_RE = re.compile(r"\bvoid\s+panic\s*\(\s*const\s+char")
PANIC_MACRO_RE = re.compile(r"#define\s+panic\s*\(\s*message\s*\)")
PANIC_CALL_RE = re.compile(r"\bpanic\s*\(")
PANICEX_CALL_RE = re.compile(r"\bpanicex\s*\(")
PANIC_MACRO_USE_RE = re.compile(r"\bPANIC(_HW|_MEM)?\s*\(")
BUG_ON_RE = re.compile(r"\bBUG_ON\s*\(")


def _skip(path: Path) -> bool:
    s = str(path).replace("\\", "/")
    return any(part in s for part in SKIP_PARTS)


def iter_sources() -> list[Path]:
    out: list[Path] = []
    for ext in ("*.c", "*.h", "*.asm"):
        for p in ROOT.rglob(ext):
            if _skip(p):
                continue
            out.append(p)
    return sorted(out)


def count_matches(text: str, pattern: re.Pattern[str]) -> int:
    return len(pattern.findall(text))


def audit_macro_definition() -> list[str]:
    errs: list[str] = []
    if not OOPS_H.is_file():
        return ["missing includes/ir0/oops.h"]
    body = OOPS_H.read_text(encoding="utf-8", errors="replace")
    if not PANIC_MACRO_RE.search(body):
        errs.append("oops.h: panic() must be #define panic(message) -> panicex(...)")
    if PANIC_FN_RE.search(body):
        errs.append("oops.h: must not declare void panic(const char ...)")
    return errs


def audit_sources() -> tuple[list[str], dict[str, int]]:
    errs: list[str] = []
    totals = {
        "panic(": 0,
        "panicex(": 0,
        "PANIC*(": 0,
        "BUG_ON(": 0,
        "void_panic_fn": 0,
    }

    for path in iter_sources():
        if path == OOPS_H:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        rel = path.relative_to(ROOT)

        if path.suffix == ".c" and PANIC_FN_RE.search(text):
            totals["void_panic_fn"] += 1
            errs.append(f"{rel}: void panic(const char ...) — use panicex/PANIC macro")

        totals["panic("] += count_matches(text, PANIC_CALL_RE)
        totals["panicex("] += count_matches(text, PANICEX_CALL_RE)
        totals["PANIC*("] += count_matches(text, PANIC_MACRO_USE_RE)
        totals["BUG_ON("] += count_matches(text, BUG_ON_RE)

    # panic() macro definition counts as one match in oops.h — subtract macro line
    oops = OOPS_H.read_text(encoding="utf-8", errors="replace")
    if PANIC_MACRO_RE.search(oops):
        totals["panic("] = max(0, totals["panic("] - 1)

    return errs, totals


def main() -> int:
    ap = argparse.ArgumentParser(description="KTM panic call-site inventory")
    ap.add_argument("--check", action="store_true", help="exit 1 on contract violations")
    ap.add_argument("--report", action="store_true", help="print counts (default if no flag)")
    args = ap.parse_args()
    if not args.check and not args.report:
        args.report = True

    errs = audit_macro_definition()
    src_errs, totals = audit_sources()
    errs.extend(src_errs)

    if args.report:
        print("KTM panic inventory")
        print(f"  panic( call sites (incl. macro uses): {totals['panic(']}")
        print(f"  PANIC/PANIC_HW/PANIC_MEM:            {totals['PANIC*(']}")
        print(f"  panicex( direct:                      {totals['panicex(']}")
        print(f"  BUG_ON(:                              {totals['BUG_ON(']}")
        print(f"  void panic() functions:                {totals['void_panic_fn']}")

    if errs:
        print(f"\n{len(errs)} violation(s):")
        for e in errs:
            print(f"  - {e}")
        return 1 if args.check else 0

    if args.check:
        print("KTM panic inventory: OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
