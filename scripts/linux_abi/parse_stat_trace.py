#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized stat traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_OK_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[stat\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+) "
    r"mode=0([0-7]+) size=(-?\d+) nlink=(\d+)"
)
AUDIT_FAIL_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[stat\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)(?! mode=)"
)


def parse_audit_lines(text: str) -> list[dict]:
    blob = re.sub(r"[\r\n]+", "", text)
    steps: list[dict] = []
    for m in AUDIT_OK_RE.finditer(blob):
        steps.append(
            {
                "step": int(m.group(1)),
                "op": m.group(2),
                "ret": int(m.group(3)),
                "errno": int(m.group(4)),
                "mode": int(m.group(5), 8),
                "size": int(m.group(6)),
                "nlink": int(m.group(7)),
                "source": "audit_line",
            }
        )
    for m in AUDIT_FAIL_RE.finditer(blob):
        if any(s["step"] == int(m.group(1)) and s["op"] == m.group(2) for s in steps):
            continue
        steps.append(
            {
                "step": int(m.group(1)),
                "op": m.group(2),
                "ret": int(m.group(3)),
                "errno": int(m.group(4)),
                "source": "audit_line",
            }
        )
    steps.sort(key=lambda x: x["step"])
    return steps


STRACE_STAT_RE = re.compile(
    r'^newfstatat\(AT_FDCWD,\s*"([^"]+)",\s*\{st_mode=([0-7]+),\s*st_size=(\d+)'
)
STRACE_FSTAT_RE = re.compile(
    r"^fstat\((-?\d+),\s*\{st_mode=([0-7]+),\st_size=(\d+)"
)


def parse_strace_stat(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        if "newfstatat" in line and "= -1" in line and "ENOENT" in line:
            steps.append(
                {
                    "step": idx,
                    "op": "stat_noent",
                    "ret": -1,
                    "errno": 2,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        if "fstat(-1" in line and "= -1" in line and "EBADF" in line:
            steps.append(
                {
                    "step": idx,
                    "op": "fstat_ebadf",
                    "ret": -1,
                    "errno": 9,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = STRACE_STAT_RE.search(line)
        if m:
            path = m.group(1)
            mode = int(m.group(2), 8)
            size = int(m.group(3))
            if "uptime" in path:
                op = "stat_proc"
            elif "noent" in path:
                continue
            else:
                op = "stat_file"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": 0,
                    "errno": 0,
                    "mode": mode,
                    "size": size,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = STRACE_FSTAT_RE.search(line)
        if m and int(m.group(1)) >= 0:
            steps.append(
                {
                    "step": idx,
                    "op": "fstat_proc",
                    "ret": 0,
                    "errno": 0,
                    "mode": int(m.group(2), 8),
                    "size": int(m.group(3)),
                    "source": "strace",
                }
            )
            idx += 1
    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: parse_stat_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_stat(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_stat(inp.parent / "strace.log")
        payload = {"side": "linux", "audit_steps": audit, "strace_steps": strace}
    elif side == "ir0":
        payload = {"side": "ir0", "audit_steps": parse_audit_lines(text)}
    else:
        print(f"unknown side: {side}", file=sys.stderr)
        return 2

    write_json(out, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
