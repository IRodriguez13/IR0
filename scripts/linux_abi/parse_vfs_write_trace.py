#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized vfs_write traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_FULL_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[vfs_write\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+) "
    r"data_hex=([0-9a-fA-F]+) stat_size=(-?\d+)"
)
AUDIT_HEX_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[vfs_write\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+) "
    r"data_hex=([0-9a-fA-F]+)(?! stat_size=)"
)
AUDIT_SIZE_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[vfs_write\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+) "
    r"stat_size=(-?\d+)(?!.*data_hex=)"
)
AUDIT_BASIC_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[vfs_write\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)(?! )"
)


def _append_step(steps: list[dict], step: dict) -> None:
    if any(s["step"] == step["step"] and s["op"] == step["op"] for s in steps):
        return
    steps.append(step)


def _match_step(line: str) -> dict | None:
    m = AUDIT_FULL_RE.search(line)
    if m:
        return {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "data_hex": m.group(5).lower(),
            "stat_size": int(m.group(6)),
            "source": "audit_line",
        }

    m = AUDIT_HEX_RE.search(line)
    if m:
        return {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "data_hex": m.group(5).lower(),
            "source": "audit_line",
        }

    m = AUDIT_SIZE_RE.search(line)
    if m:
        return {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "stat_size": int(m.group(5)),
            "source": "audit_line",
        }

    m = AUDIT_BASIC_RE.search(line)
    if m:
        return {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "source": "audit_line",
        }

    return None


def parse_audit_lines(text: str) -> list[dict]:
    steps: list[dict] = []

    for raw in text.splitlines():
        line = raw.strip()
        if "[LINUX_ABI_AUDIT][vfs_write]" not in line:
            continue
        step = _match_step(line)
        if step:
            _append_step(steps, step)

    steps.sort(key=lambda x: (x["step"], x["op"]))
    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: parse_vfs_write_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        payload = {
            "side": "linux",
            "audit_steps": parse_audit_lines(text),
            "strace_steps": [],
        }
    elif side == "ir0":
        payload = {"side": "ir0", "audit_steps": parse_audit_lines(text)}
    else:
        print(f"unknown side: {side}", file=sys.stderr)
        return 2

    write_json(out, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
