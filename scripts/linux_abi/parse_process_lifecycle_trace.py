#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Parse Linux strace and IR0 serial audit lines into process_lifecycle traces."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

AUDIT_RE = re.compile(
    r"\[LINUX_ABI_AUDIT\]\[process_lifecycle\] step=(\d+) op=([\w_]+) ret=(-?\d+) errno=(\d+)"
    r"(?: status=0x([0-9a-fA-F]+))?"
    r"(?: flags=([\w_]+))?"
)

SERIAL_SMOKE_TAGS = ["PROC_LIFECYCLEOK"]


def _normalize_serial_audit(text: str) -> str:
    out = text
    blob = re.sub(r"[\r\n]+", "", text)
    for tag in ["[LINUX_ABI_AUDIT][process_lifecycle]", "[PROC_LIFECYCLEOK]"]:
        if tag in blob:
            continue
        for i in range(1, len(tag)):
            frag = tag[:i] + tag[i:]
            if frag in blob.replace(" ", ""):
                pass
            needle = tag[:i]
            for sep in ("\n", "\r\n", " ", "\t"):
                frag = tag[:i] + sep + tag[i:]
                if frag in out:
                    out = out.replace(frag, tag)
    # Rejoin op= lines split mid-token (common on IR0 serial).
    out = re.sub(
        r"\[LINUX_ABI_AUDIT\]\[process_lifecycle\] step=(\d+) op=\s*\n?\s*([\w_]+)",
        r"[LINUX_ABI_AUDIT][process_lifecycle] step=\1 op=\2",
        out,
    )
    out = re.sub(
        r"(op=[\w_]+) ret=\s*\n?\s*(-?\d+)",
        r"\1 ret=\2",
        out,
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
        if "[LINUX_ABI_AUDIT][process_lifecycle]" not in line:
            continue
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
        if m.group(6):
            step["flags"] = m.group(6)
        steps.append(step)
    steps.sort(key=lambda x: (x["step"], x["op"]))
    return steps


def write_json(path: Path, payload: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(payload, indent=2) + "\n")


def main() -> int:
    if len(sys.argv) < 4:
        print(
            "usage: parse_process_lifecycle_trace.py {linux|ir0} INPUT OUTPUT.json",
            file=sys.stderr,
        )
        return 2

    side = sys.argv[1]
    inp = Path(sys.argv[2])
    out = Path(sys.argv[3])
    text = inp.read_text(errors="replace") if inp.is_file() else ""

    payload = {
        "side": side,
        "audit_steps": parse_audit_lines(text),
    }
    if side == "linux":
        strace_path = inp.with_name("strace.log")
        if strace_path.is_file():
            payload["strace_log"] = str(strace_path)

    write_json(out, payload)
    return 0


if __name__ == "__main__":
    sys.exit(main())
