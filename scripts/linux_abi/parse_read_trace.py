#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized read traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[read\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)"
    r"(?: data_hex=([0-9a-fA-F]+))?"
)


def _normalize_serial_audit(text: str) -> str:
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][read]" in line or buf:
            buf += line
            if "errno=" in buf:
                if "data_hex=" in buf or re.search(r"op=read_(eof|ebadf|pipe)", buf):
                    out.append(buf)
                    buf = ""
            continue
        out.append(line)
    if buf:
        out.append(buf)
    return "\n".join(out)


STRACE_READ_EMPTY_RE = re.compile(r"^read\((-?\d+),\s*(\[\.\.\.\]|0x[0-9a-fA-F]+),\s*(\d+)\)\s*=\s*(-?\d+)")


def parse_audit_lines(text: str) -> list[dict]:
    text = _normalize_serial_audit(text)
    steps: list[dict] = []
    for line in text.splitlines():
        m = AUDIT_RE.search(line)
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
            step["data_hex"] = m.group(5).lower()
        steps.append(step)
    steps.sort(key=lambda x: x["step"])
    return steps


def parse_strace_read(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = re.match(r'^read\((-?\d+),\s*"([^"]*)",\s*(\d+)\)\s*=\s*(-?\d+)', line)
        if m:
            fd = int(m.group(1))
            data = m.group(2)
            count = int(m.group(3))
            ret = int(m.group(4))
            op = "read_pipe" if fd == 0 and ret > 0 else "read_eof" if fd == 0 and ret == 0 else "read_ebadf" if fd < 0 else "read"
            step = {
                "step": idx,
                "op": op,
                "ret": ret,
                "errno": 0 if ret >= 0 else -ret,
                "source": "strace",
            }
            if data:
                step["data_hex"] = data.encode("latin1", "replace").hex()
            steps.append(step)
            idx += 1
            continue
        m = STRACE_READ_EMPTY_RE.match(line)
        if m:
            fd = int(m.group(1))
            ret = int(m.group(4))
            op = "read_eof" if fd == 0 and ret == 0 else "read_ebadf" if fd < 0 else "read"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": ret,
                    "errno": 0 if ret >= 0 else -ret,
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
            "usage: parse_read_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_read(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_read(inp.parent / "strace.log")
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
