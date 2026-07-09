#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse [LINUX_ABI_AUDIT][CONTRACT] lines into normalized step traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path


def make_audit_re(contract: str) -> re.Pattern[str]:
    return re.compile(
        rf"\[LINUX_ABI_AUDIT\]\[{re.escape(contract)}\] "
        r"step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)"
        r"(?: path=([^\s]+))?"
        r"(?: data_hex=([0-9a-fA-F]+))?"
        r"(?: revents=(\d+))?"
    )


def parse_audit_lines(text: str, contract: str) -> list[dict]:
    audit_re = make_audit_re(contract)
    steps: list[dict] = []
    for line in text.splitlines():
        m = audit_re.search(line)
        if not m:
            continue
        step = {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "source": "audit_line",
        }
        if m.group(5):
            step["path"] = m.group(5)
        if m.group(6):
            step["data_hex"] = m.group(6).lower()
        if m.group(7):
            step["revents"] = int(m.group(7))
        steps.append(step)
    steps.sort(key=lambda x: x["step"])
    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 5:
        print(
            "usage: parse_simple_trace.py CONTRACT {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    contract = sys.argv[1]
    side = sys.argv[2]
    inp = Path(sys.argv[3])
    out = Path(sys.argv[4])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side not in ("linux", "ir0"):
        print(f"unknown side: {side}", file=sys.stderr)
        return 2

    payload = {
        "side": side,
        "contract": contract,
        "audit_steps": parse_audit_lines(text, contract),
    }
    write_json(out, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
