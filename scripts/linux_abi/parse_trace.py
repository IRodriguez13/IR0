#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized brk traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[brk\] step=(\d+) ret=(-?\d+) errno=(\d+)"
)
DONE_RE = re.compile(r"\[LINUX_ABI_AUDIT\]\[brk\] DONE|\[ABIBRKOK\]|\[BRKOK\]")


def _normalize_serial_audit(text: str) -> str:
    """QEMU serial may wrap mid-line; collapse audit fragments onto one line."""
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][brk]" in line or buf:
            buf += line
            if "errno=" in buf or DONE_RE.search(buf):
                out.append(buf)
                buf = ""
            continue
        out.append(line)
    if buf:
        out.append(buf)
    return "\n".join(out)
STRACE_BRK_RE = re.compile(
    r"^brk\((NULL|0x[0-9a-fA-F]+|\d+)\)\s*=\s*(-?0x[0-9a-fA-F]+|-?\d+)"
)


def _parse_int(token: str) -> int:
    token = token.strip()
    if token.startswith("-0x") or token.startswith("0x") or token.startswith("-0X"):
        return int(token, 16)
    return int(token, 10)


def parse_audit_lines(text: str) -> list[dict]:
    text = _normalize_serial_audit(text)
    steps: list[dict] = []
    for line in text.splitlines():
        m = AUDIT_RE.search(line)
        if not m:
            continue
        steps.append(
            {
                "step": int(m.group(1)),
                "ret": int(m.group(2)),
                "errno": int(m.group(3)),
                "source": "audit_line",
            }
        )
    steps.sort(key=lambda x: x["step"])
    return steps


def parse_strace_brk(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    idx = 0
    if not strace_log.is_file():
        return steps
    for line in strace_log.read_text(errors="replace").splitlines():
        line = line.strip()
        if "<unfinished" in line or "resumed" in line:
            continue
        m = STRACE_BRK_RE.search(line)
        if not m:
            continue
        arg = m.group(1)
        ret = _parse_int(m.group(2))
        steps.append(
            {
                "step": idx,
                "arg": None if arg == "NULL" else _parse_int(arg),
                "ret": ret,
                "errno": 0 if ret >= 0 else -ret,
                "source": "strace",
                "raw": line,
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
            "usage: parse_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_brk(inp.with_name("strace.log"))
        if not strace and inp.name != "combined.log":
            strace = parse_strace_brk(inp.parent / "strace.log")
        payload = {
            "side": "linux",
            "audit_steps": audit,
            "strace_steps": strace,
        }
    elif side == "ir0":
        payload = {
            "side": "ir0",
            "audit_steps": parse_audit_lines(text),
        }
    else:
        print(f"unknown side: {side}", file=sys.stderr)
        return 2

    write_json(out, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
