#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into kill_sigterm traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[kill_sigterm\] step=(\d+) op=([\w_]+) ret=(-?\d+) errno=(\d+)"
    r"(?: status=0x([0-9a-fA-F]+))?"
)
DONE_RE = re.compile(r"\[KILLSIGTERMOK\]")

STRACE_FORK_RE = re.compile(r"^fork\(\)\s*=\s*(-?\d+)")
STRACE_KILL_RE = re.compile(r"^kill\((-?\d+),\s*SIGTERM\)\s*=\s*(-?\d+)")
STRACE_WAIT_SIGNALED_RE = re.compile(
    r"wait4(?: resumed)?>\[\{WIFSIGNALED\(s\) && WTERMSIG\(s\) == (\d+)\}\], 0, NULL\) = (-?\d+)"
)
STRACE_WAIT_PLAIN_SIGNALED_RE = re.compile(
    r"^wait4\((-?\d+),\s*\[\{WIFSIGNALED\(s\) && WTERMSIG\(s\) == (\d+)\}\]\)\s*=\s*(-?\d+)"
)


def _normalize_serial_audit(text: str) -> str:
    out = re.sub(
        r"\[LINUX_ABI_AUDIT\]\[kill_sigterm\] step=(\d+) op=\s*\n?\s*([\w_]+)",
        r"[LINUX_ABI_AUDIT][kill_sigterm] step=\1 op=\2",
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


def parse_strace_kill_sigterm(strace_log: Path) -> list[dict]:
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

        m = STRACE_KILL_RE.match(line)
        if m and fork_pid is not None:
            step_no += 1
            steps.append(
                {
                    "step": step_no,
                    "op": "kill",
                    "ret": int(m.group(2)),
                    "errno": 0 if int(m.group(2)) == 0 else 1,
                    "source": "strace",
                }
            )
            continue

        m = STRACE_WAIT_SIGNALED_RE.search(line)
        if m:
            sig = int(m.group(1))
            pid = int(m.group(2))
            step_no += 1
            steps.append(
                {
                    "step": step_no,
                    "op": "kill_sigterm",
                    "ret": pid,
                    "errno": 0,
                    "status": sig,
                    "source": "strace",
                }
            )
            continue

        m = STRACE_WAIT_PLAIN_SIGNALED_RE.match(line)
        if m:
            pid = int(m.group(3))
            sig = int(m.group(2))
            step_no += 1
            steps.append(
                {
                    "step": step_no,
                    "op": "kill_sigterm",
                    "ret": pid,
                    "errno": 0,
                    "status": sig,
                    "source": "strace",
                }
            )

    return steps


def build_trace(source: str, stdout: Path, strace: Path | None) -> dict:
    text = stdout.read_text(errors="replace")
    audit_steps = parse_audit_lines(text)
    if source == "linux" and strace and strace.is_file():
        strace_steps = parse_strace_kill_sigterm(strace)
        if len(strace_steps) >= len(audit_steps):
            audit_steps = strace_steps
    done = bool(DONE_RE.search(text))
    return {
        "source": source,
        "audit_steps": audit_steps,
        "done_tag": done,
    }


def main() -> int:
    if len(sys.argv) != 5:
        print(
            f"usage: {sys.argv[0]} <linux|ir0> <stdout.log> <strace.log|-> <trace.json>",
            file=sys.stderr,
        )
        return 2

    source = sys.argv[1]
    stdout = Path(sys.argv[2])
    strace_arg = sys.argv[3]
    out_json = Path(sys.argv[4])
    strace = None if strace_arg == "-" else Path(strace_arg)

    trace = build_trace(source, stdout, strace)
    out_json.write_text(json.dumps(trace, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
