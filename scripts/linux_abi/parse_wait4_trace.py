#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into normalized wait4 traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[wait4\] step=(\d+) op=(\w+) ret=(-?\d+) errno=(\d+)"
    r"(?: status=0x([0-9a-fA-F]+))?"
)
DONE_RE = re.compile(r"\[WAIT4OK\]")


def _normalize_serial_audit(text: str) -> str:
    out: list[str] = []
    buf = ""
    for line in text.splitlines():
        if "[LINUX_ABI_AUDIT][wait4]" in line or buf:
            buf += line
            if "errno=" in buf and ("status=0x" in buf or "wait4]" in buf and "fork" in buf):
                if "op=wait4" in buf or "op=fork" in buf:
                    out.append(buf)
                    buf = ""
                elif "errno=" in buf and "op=fork" in buf and "status=" not in buf:
                    out.append(buf)
                    buf = ""
            continue
        out.append(line)
    if buf:
        out.append(buf)
    return "\n".join(out)


STRACE_FORK_RE = re.compile(r"^fork\(\)\s*=\s*(-?\d+)")
STRACE_WAIT4_DONE_RE = re.compile(
    r"wait4(?: resumed)?>\[\{WIFEXITED\(s\) && WEXITSTATUS\(s\) == (\d+)\}\], 0, NULL\) = (-?\d+)"
)
STRACE_WAIT4_PLAIN_RE = re.compile(
    r"^wait4\((-?\d+),\s*\[\{WIFEXITED\(s\) && WEXITSTATUS\(s\) == (\d+)\}\]\)\s*=\s*(-?\d+)"
)


def _parse_status_bracket(bracket: str) -> int | None:
    m = re.search(r"WEXITSTATUS\(s\) == (\d+)", bracket)
    if m:
        code = int(m.group(1))
        return code << 8
    m = re.search(r"0x([0-9a-fA-F]+)", bracket)
    if m:
        return int(m.group(1), 16)
    return None


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
            step["status"] = int(m.group(5), 16)
        steps.append(step)
    steps.sort(key=lambda x: x["step"])
    return steps


def parse_strace_wait4(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    fork_pid: int | None = None
    if not strace_log.is_file():
        return steps
    for line in strace_log.read_text(errors="replace").splitlines():
        line = re.sub(r"^\d+\s+", "", line.strip())
        if "<unfinished" in line or "resumed" in line:
            continue
        m = STRACE_FORK_RE.search(line)
        if m:
            fork_pid = int(m.group(1))
            steps.append(
                {
                    "step": 0,
                    "op": "fork",
                    "ret": fork_pid,
                    "errno": 0 if fork_pid >= 0 else -fork_pid,
                    "source": "strace",
                }
            )
            continue
        m = STRACE_WAIT4_DONE_RE.search(line)
        if m:
            exit_code = int(m.group(1))
            wret = int(m.group(2))
            steps.append(
                {
                    "step": 1,
                    "op": "wait4",
                    "ret": wret,
                    "errno": 0 if wret >= 0 else -wret,
                    "status": exit_code << 8,
                    "source": "strace",
                }
            )
            continue
        m = STRACE_WAIT4_PLAIN_RE.search(line)
        if m:
            exit_code = int(m.group(2))
            wret = int(m.group(3))
            steps.append(
                {
                    "step": 1,
                    "op": "wait4",
                    "ret": wret,
                    "errno": 0 if wret >= 0 else -wret,
                    "status": exit_code << 8,
                    "source": "strace",
                }
            )
            continue
    if fork_pid is not None and len(steps) >= 2:
        steps[1]["expected_pid"] = fork_pid
    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: parse_wait4_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    if side == "linux":
        audit = parse_audit_lines(text)
        strace = parse_strace_wait4(inp.with_name("strace.log"))
        if not strace and inp.name != "combined.log":
            strace = parse_strace_wait4(inp.parent / "strace.log")
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
