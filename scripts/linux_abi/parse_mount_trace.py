#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized mount traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[mount\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+) path=([^\s]+)"
    r"(?: data_hex=([0-9a-fA-F]+))?"
)


def _normalize_serial_audit(text: str) -> str:
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][mount]" in line or buf:
            buf += line
            if "path=" in buf and "errno=" in buf:
                if "data_hex=" in buf or "op=" in buf:
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
            "ret": int(m.group(3)),
            "errno": int(m.group(4)),
            "path": m.group(5),
            "source": "audit_line",
        }
        if m.group(6):
            step["data_hex"] = m.group(6).lower()
        steps.append(step)
    steps.sort(key=lambda x: x["step"])
    return steps


def parse_strace_mount(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    if not strace_log.is_file():
        return steps
    idx = 0
    for raw in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", raw.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = re.match(
            r'^mount\("([^"]*)",\s*"([^"]*)",\s*"([^"]*)",\s*[^,]+,\s*[^)]*\)\s*=\s*(-?\d+)',
            line,
        )
        if m:
            fstype = m.group(3)
            path = m.group(2)
            ret = int(m.group(4))
            if fstype == "tmpfs" and "nope" in path:
                op = "mount_noent"
            elif fstype == "tmpfs":
                op = "mount_tmpfs"
            elif fstype == "badfs":
                op = "mount_badfs"
            else:
                op = "mount"
            steps.append(
                {
                    "step": idx,
                    "op": op,
                    "ret": ret,
                    "errno": 0 if ret >= 0 else -ret,
                    "path": path,
                    "source": "strace",
                }
            )
            idx += 1
            continue
        m = re.match(r'^umount2?\("([^"]+)"(?:,\s*[^)]+)?\)\s*=\s*(-?\d+)', line)
        if m:
            steps.append(
                {
                    "step": idx,
                    "op": "umount_tmpfs",
                    "ret": int(m.group(2)),
                    "errno": 0,
                    "path": m.group(1),
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
            "usage: parse_mount_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_mount(inp.with_name("strace.log"))
        if not strace:
            strace = parse_strace_mount(inp.parent / "strace.log")
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
