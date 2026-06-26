#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized mmap traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[mmap\] step=(\d+) op=(\w+) ret=0x([0-9a-fA-F]+) errno=(\d+) len=(\d+) req=0x([0-9a-fA-F]+)"
    r"(?: data_hex=([0-9a-fA-F]+))?"
)


def _normalize_serial_audit(text: str) -> str:
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][mmap]" in line or buf:
            buf += line
            if "req=0x" in buf and ("data_hex=" in buf or "len=" in buf):
                if buf.count("op=") >= 1 and buf.count("errno=") >= 1:
                    out.append(buf)
                    buf = ""
            continue
        out.append(line)
    if buf:
        out.append(buf)
    return "\n".join(out)


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
            "ret": int(m.group(3), 16),
            "errno": int(m.group(4)),
            "len": int(m.group(5)),
            "req": int(m.group(6), 16),
            "source": "audit_line",
        }
        if m.group(7):
            step["data_hex"] = m.group(7).lower()
        steps.append(step)
    steps.sort(key=lambda x: x["step"])
    return steps


def parse_strace_mmap(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = re.match(
            r"^mmap\(NULL,\s*(\d+),\s*PROT_([^,]+),\s*MAP_([^,]+),\s*(-?\d+),\s*0\)\s*=\s*(0x[0-9a-fA-F]+)",
            line,
        )
        if m:
            length = int(m.group(1))
            prot = m.group(2)
            flags = m.group(3)
            ret = int(m.group(5), 16)
            if "ANONYMOUS" in flags and "NONE" in prot:
                op = "mmap_anon_none"
            else:
                op = "mmap_anon_rw"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": ret,
                    "errno": 0,
                    "len": length,
                    "req": 0,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = re.match(
            r"^mmap\(0x([0-9a-fA-F]+),\s*(\d+),\s*PROT_[^,]+,\s*MAP_[^)]+\)\s*=\s*(0x[0-9a-fA-F]+)",
            line,
        )
        if m:
            req = int(m.group(1), 16)
            length = int(m.group(2))
            ret = int(m.group(3), 16)
            steps.append(
                {
                    "step": idx,
                    "op": "mmap_fixed",
                    "ret": ret,
                    "errno": 0,
                    "len": length,
                    "req": req,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = re.match(r"^munmap\(0x([0-9a-fA-F]+),\s*(\d+)\)\s*=\s*(-?\d+)", line)
        if m:
            ret = int(m.group(3))
            steps.append(
                {
                    "step": idx,
                    "op": "munmap_rw",
                    "ret": ret if ret >= 0 else 0xFFFFFFFFFFFFFFFF,
                    "errno": 0 if ret >= 0 else -ret,
                    "len": int(m.group(2)),
                    "req": int(m.group(1), 16),
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
            "usage: parse_mmap_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_mmap(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_mmap(inp.parent / "strace.log")
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
