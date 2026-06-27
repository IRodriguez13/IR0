#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into wait4_wnohang traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[wait4_wnohang\] step=(\d+) op=([\w_]+) ret=(-?\d+) errno=(\d+)"
    r"(?: status=0x([0-9a-fA-F]+))?"
)
DONE_RE = re.compile(r"\[WAIT4WNOHANGOK\]")

STRACE_FORK_RE = re.compile(r"^fork\(\)\s*=\s*(-?\d+)")
STRACE_WAIT_WNOHANG_ZERO_RE = re.compile(
    r"^wait4\((-?\d+), .+, WNOHANG, NULL\) = 0"
)
STRACE_WAIT_BLOCK_RE = re.compile(
    r"wait4 resumed>\[\{WIFEXITED\(s\) && WEXITSTATUS\(s\) == (\d+)\}\], 0, NULL\) = (-?\d+)"
)
STRACE_WAIT_WNOHANG_ECHILD_RE = re.compile(
    r"^wait4\((-?\d+), .+, WNOHANG, NULL\) = -1 ECHILD"
)


def _normalize_serial_audit(text: str) -> str:
    out = re.sub(
        r"\[LINUX_ABI_AUDIT\]\[wait4_wnohang\] step=(\d+) op=\s*\n?\s*([\w_]+)",
        r"[LINUX_ABI_AUDIT][wait4_wnohang] step=\1 op=\2",
        text,
    )
    out = re.sub(
        r"(errno=\d+) status=0x\s*\n?\s*([0-9a-fA-F]+)",
        r"\1 status=0x\2",
        out,
    )
    return out


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


def parse_strace_wait4_wnohang(strace_log: Path) -> list[dict]:
    steps: list[dict] = []
    fork_pid: int | None = None
    step_no = 0

    for line in strace_log.read_text(errors="replace").splitlines():
        line = line.split("  ", 1)[-1].strip() if "  " in line else line.strip()

        m = STRACE_FORK_RE.match(line)
        if m:
            fork_pid = int(m.group(1))
            if fork_pid > 0:
                step_no += 1
                steps.append(
                    {
                        "step": step_no,
                        "op": "fork",
                        "ret": fork_pid,
                        "errno": 0,
                        "source": "strace",
                    }
                )
            continue

        m = STRACE_WAIT_WNOHANG_ZERO_RE.search(line)
        if m and fork_pid is not None and int(m.group(1)) == fork_pid:
            step_no += 1
            steps.append(
                {
                    "step": step_no,
                    "op": "wait4_wnohang_alive",
                    "ret": 0,
                    "errno": 0,
                    "source": "strace",
                }
            )
            continue

        m = STRACE_WAIT_BLOCK_RE.search(line)
        if m and fork_pid is not None and int(m.group(2)) == fork_pid:
            step_no += 1
            exit_code = int(m.group(1))
            steps.append(
                {
                    "step": step_no,
                    "op": "wait4_block_reap",
                    "ret": fork_pid,
                    "errno": 0,
                    "status": exit_code << 8,
                    "source": "strace",
                }
            )
            continue

        m = STRACE_WAIT_WNOHANG_ECHILD_RE.search(line)
        if m:
            step_no += 1
            steps.append(
                {
                    "step": step_no,
                    "op": "wait4_wnohang_echild",
                    "ret": -1,
                    "errno": 10,
                    "source": "strace",
                }
            )

    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: parse_wait4_wnohang_trace.py {linux|ir0} stdout.log [strace.log] out.json",
            file=sys.stderr,
        )
        return 2

    mode = sys.argv[1]
    stdout_path = Path(sys.argv[2])
    text = stdout_path.read_text(errors="replace")

    if mode == "linux":
        strace_path = Path(sys.argv[3])
        out_path = Path(sys.argv[4])
        audit_steps = parse_strace_wait4_wnohang(strace_path)
        if not audit_steps:
            audit_steps = parse_audit_lines(text)
    else:
        out_path = Path(sys.argv[3])
        audit_steps = parse_audit_lines(text)

    payload = {
        "contract": "wait4_wnohang",
        "mode": mode,
        "audit_steps": audit_steps,
        "done_tag": bool(DONE_RE.search(text)),
    }
    write_json(out_path, payload)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
