#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host contract for the IR0 userspace KTAP parser."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import ir0_selftest_tap as tap  # noqa: E402


def main() -> int:
    planned, failures = tap.parse_ktap(
        "KTAP version 1\n1..2\nok 1 open_existing\nok 2 close_ok\n"
    )
    if planned != 2 or failures:
        print("valid plan failed", file=sys.stderr)
        return 1
    try:
        tap.parse_ktap("KTAP version 1\n1..1\nnot ok 1 open_existing\n")
        planned, failures = tap.parse_ktap(
            "KTAP version 1\n1..1\nnot ok 1 open_existing\n"
        )
        if not failures:
            print("not ok was accepted", file=sys.stderr)
            return 1
    except ValueError:
        print("not ok raised unexpectedly", file=sys.stderr)
        return 1
    try:
        tap.parse_ktap("KTAP version 1\n1..2\nok 1 open_existing\n")
        print("incomplete plan was accepted", file=sys.stderr)
        return 1
    except ValueError:
        pass
    print("selftest tap contract ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
