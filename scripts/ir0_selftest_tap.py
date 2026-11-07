#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse bounded KTAP/TAP output from IR0 userspace selftests."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ktm_host_safety as safety  # noqa: E402

PLAN_RE = re.compile(r"^1\.\.(\d+)\s*$")
RESULT_RE = re.compile(
    r"^(ok|not ok)\s+(\d+)(?:\s+(.*?))?(?:\s+#\s*(SKIP|XFAIL|TIMEOUT|ERROR)\b.*)?\s*$"
)
VERSION_RE = re.compile(r"^(KTAP version 1|TAP version 13)\s*$")


def parse_ktap(text: str) -> tuple[int, list[str]]:
    planned = None
    results: list[tuple[str, int, str, str | None]] = []
    for raw in text.splitlines():
        line = raw.strip("\r")
        if VERSION_RE.match(line):
            continue
        plan = PLAN_RE.match(line)
        if plan:
            planned = int(plan.group(1))
            continue
        result = RESULT_RE.match(line)
        if result:
            status, number, name, directive = result.groups()
            results.append((status, int(number), name or "", directive))
    if planned is None:
        raise ValueError("missing TAP plan")
    if len(results) != planned:
        raise ValueError(f"planned {planned} tests but saw {len(results)}")
    failures = [
        f"{number} {name}".strip()
        for status, number, name, directive in results
        if status == "not ok" and directive != "SKIP"
    ]
    return planned, failures


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log")
    args = parser.parse_args()
    path = Path(args.log)
    text = safety.read_log_text(path)
    try:
        planned, failures = parse_ktap(text)
    except ValueError as exc:
        print(f"ir0-selftest: {exc}", file=sys.stderr)
        return 1
    if failures:
        print("ir0-selftest: failed: " + ", ".join(failures), file=sys.stderr)
        return 1
    print(f"ir0-selftest: {planned} tests ok")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
