#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode KTM flight recorder dumps from QEMU serial logs."""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

FLIGHT_BEGIN = re.compile(r"--- KTM FLIGHT RECORDER")
FLIGHT_END = re.compile(r"--- END KTM FLIGHT RECORDER")
ENTRY_RE = re.compile(
    r"\[([0-9A-Fa-f]+)\]\s+cpu([0-9A-Fa-f]+)\s+pid=([0-9A-Fa-f]+)\s+"
    r"(syscall_enter|syscall_ret|sched_switch|panic|invariant|ev)\s+"
    r"a0=([0-9A-Fa-f]+)\s+a1=([0-9A-Fa-f]+)\s+a2=([0-9A-Fa-f]+)\s+a3=([0-9A-Fa-f]+)"
)

SYSCALL_NAMES: dict[int, str] = {
    0: "read",
    1: "write",
    3: "close",
    8: "lseek",
    9: "mmap",
    12: "brk",
    22: "pipe",
    32: "dup",
    33: "dup2",
    57: "fork",
    61: "execve",
    62: "kill",
    72: "fcntl",
    79: "getcwd",
    80: "chdir",
    230: "wait4",
    231: "exit_group",
    257: "openat",
    280: "utimensat",
}


def _hex32(token: str) -> int:
    return int(token, 16)


def _fmt_syscall(etype: str, a0: int, a1: int, a2: int, a3: int) -> str:
    if etype == "syscall_enter":
        name = SYSCALL_NAMES.get(a0, f"nr{a0}")
        return f"enter {name} arg1=0x{a1:x} arg2=0x{a2:x} arg3=0x{a3:x}"
    if etype == "syscall_ret":
        name = SYSCALL_NAMES.get(a0, f"nr{a0}")
        ret = a1 if a1 < (1 << 63) else a1 - (1 << 64)
        return f"ret  {name} -> {ret} (0x{a1:x})"
    if etype == "sched_switch":
        return f"sched {a0} -> {a1}"
    return f"{etype} a0=0x{a0:x} a1=0x{a1:x}"


def extract_flight_block(text: str) -> str:
    lines: list[str] = []
    inside = False
    for line in text.splitlines():
        if FLIGHT_BEGIN.search(line):
            inside = True
            continue
        if inside and FLIGHT_END.search(line):
            break
        if inside:
            lines.append(line)
    return "\n".join(lines)


def decode_block(block: str, *, tail: int) -> list[dict]:
    entries: list[dict] = []
    for line in block.splitlines():
        m = ENTRY_RE.search(line)
        if not m:
            continue
        seq = _hex32(m.group(1))
        pid = _hex32(m.group(3))
        etype = m.group(4)
        a0, a1, a2, a3 = map(_hex32, m.groups()[4:8])
        entries.append(
            {
                "seq": seq,
                "pid": pid,
                "type": etype,
                "a0": a0,
                "a1": a1,
                "a2": a2,
                "a3": a3,
                "summary": _fmt_syscall(etype, a0, a1, a2, a3),
            }
        )
    if tail > 0:
        return entries[-tail:]
    return entries


def main() -> int:
    ap = argparse.ArgumentParser(description="Decode KTM flight recorder from serial log")
    ap.add_argument("log", type=Path, nargs="?", help="Serial log (default: stdin)")
    ap.add_argument("--tail", type=int, default=24, help="Last N flight events to show")
    args = ap.parse_args()

    if args.log and args.log.is_file():
        text = args.log.read_text(errors="replace")
    elif args.log:
        print(f"log missing: {args.log}", file=sys.stderr)
        return 1
    else:
        text = sys.stdin.read()

    block = extract_flight_block(text)
    if not block.strip():
        print("KTM_FLIGHT_DECODE: no flight recorder block in log")
        return 1

    entries = decode_block(block, tail=args.tail)
    print(f"KTM_FLIGHT_DECODE events={len(entries)} (tail={args.tail})")
    for e in entries:
        print(
            f"  [{e['seq']:08x}] pid={e['pid']:5d} {e['summary']}"
        )
    return 0


if __name__ == "__main__":
    sys.exit(main())
