#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized openat traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[openat\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)"
)

STRACE_OPENAT_RE = re.compile(
    r'^openat\((AT_FDCWD|-?\d+),\s*"([^"]+)",\s*([A-Z_|]+)(?:,\s*(\d+))?\)\s*=\s*(-?\d+)'
)
STRACE_OPENAT_FAIL_RE = re.compile(
    r'^openat\(AT_FDCWD,\s*"([^"]+)",\s*([A-Z_|]+)(?:,\s*(\d+))?\)\s*=\s*-1\s+\w+\s+\((\w+)\)'
)
STRACE_CLOSE_RE = re.compile(r"^close\((-?\d+)\)\s*=\s*(-?\d+)")
STRACE_CLOSE_FAIL_RE = re.compile(
    r"^close\((-?\d+)\)\s*=\s*-1\s+\w+\s+\((\w+)\)"
)


def parse_audit_lines(text: str) -> list[dict]:
    blob = re.sub(r"[\r\n]+", "", text)
    steps: list[dict] = []
    for m in AUDIT_RE.finditer(blob):
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


def _errno_name(name: str) -> int:
    table = {"ENOENT": 2, "EBADF": 9, "EACCES": 13}
    return table.get(name, 0)


def parse_strace_openat(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = STRACE_OPENAT_FAIL_RE.match(line)
        if m:
            path = m.group(1)
            err = _errno_name(m.group(4))
            op = "open_noent" if "noent" in path else "open_fail"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": -1,
                    "errno": err,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = STRACE_OPENAT_RE.match(line)
        if m:
            path = m.group(2)
            ret = int(m.group(5))
            if ret >= 0 and "noent" not in path:
                steps.append(
                    {
                        "step": idx,
                        "op": "open_existing",
                        "ret": ret,
                        "errno": 0,
                        "source": "strace",
                    }
                )
                idx += 1
            continue
        m = STRACE_CLOSE_FAIL_RE.match(line)
        if m:
            fd = int(m.group(1))
            err = _errno_name(m.group(2))
            if fd < 0:
                steps.append(
                    {
                        "step": idx,
                        "op": "close_ebadf",
                        "ret": -1,
                        "errno": err,
                        "source": "strace",
                    }
                )
                idx += 1
            continue
        m = STRACE_CLOSE_RE.match(line)
        if m:
            fd = int(m.group(1))
            ret = int(m.group(2))
            if fd >= 0 and ret == 0:
                steps.append(
                    {
                        "step": idx,
                        "op": "close_ok",
                        "ret": 0,
                        "errno": 0,
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
            "usage: parse_openat_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_openat(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_openat(inp.parent / "strace.log")
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
