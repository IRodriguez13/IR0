#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized execve traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[execve\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)"
    r"(?: status=0x([0-9a-fA-F]+))?"
)

STRACE_EXECVE_OK_RE = re.compile(
    r'^execve\("([^"]+)",\s*\[[^\]]*\]\)\s*=\s*0'
)
STRACE_EXECVE_FAIL_RE = re.compile(
    r'^execve\("([^"]+)",\s*\[[^\]]*\]\)\s*=\s*-1\s+(\w+)\s+\((\w+)\)'
)


def _normalize_serial_audit(text: str) -> str:
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][execve]" in line or buf:
            buf += line
            if "errno=" in buf and ("status=0x" in buf or "op=helper_run" in buf):
                out.append(buf)
                buf = ""
            elif "errno=" in buf and "op=execve_noent" in buf:
                out.append(buf)
                buf = ""
            elif "errno=" in buf and "op=execve_ok" in buf and "status=" not in buf:
                out.append(buf)
                buf = ""
            continue
        out.append(line)
    if buf:
        out.append(buf)
    return "\n".join(out)


def parse_audit_lines(text: str) -> list[dict]:
    # Serial interleave may split audit lines; scan a newline-stripped blob.
    blob = re.sub(r"[\r\n]+", "", text)
    steps: list[dict] = []
    for m in AUDIT_RE.finditer(blob):
        step = {
            "step": int(m.group(1)),
            "op": m.group(2),
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "source": "audit_line",
        }
        if m.group(5):
            step["status"] = int(m.group(5), 16)
        steps.append(step)
    steps.sort(key=lambda x: (x["op"], x["step"]))
    return steps


def parse_strace_execve(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = STRACE_EXECVE_FAIL_RE.match(line)
        if m:
            path = m.group(1)
            err_name = m.group(3)
            errno = 2 if err_name == "ENOENT" else 0
            steps.append(
                {
                    "step": idx,
                    "op": "execve_noent",
                    "ret": -1,
                    "errno": errno,
                    "path": path,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = STRACE_EXECVE_OK_RE.match(line)
        if m:
            path = m.group(1)
            op = "helper_run" if "exec_helper" in path or "helper" in path else "execve_ok"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": 0,
                    "errno": 0,
                    "path": path,
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
            "usage: parse_execve_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_execve(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_execve(inp.parent / "strace.log")
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
