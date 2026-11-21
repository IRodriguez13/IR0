#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host contract for the LTP serial-log parser (scripts/ltp_harness.py)."""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

import ltp_harness as ltp  # noqa: E402


def parse(tmp: Path, name: str, tags: list[str], text: str):
    log = tmp / name
    log.write_text(text)
    (tmp / (name + ".tags")).write_text("".join(f"{t}\n" for t in tags))
    return ltp.parse_log(log, None)


def main() -> int:
    tags = ["getpid01", "fork01", "read01", "write01"]
    with tempfile.TemporaryDirectory() as tmp_name:
        tmp = Path(tmp_name)
        rows, unreached = parse(
            tmp, "ltp-x86_64-0.log", tags,
            "boot\nLTP_BEGIN getpid01\nTPASS\n"
            "LTP_RESULT getpid01 PASS exit=0 sig=0 ms=1\n"
            "out without newline LTP_BEGIN fork01\n"
            "LTP_RESULT fork01 TCONF exit=32 sig=0 ms=1\n"
            "LTP_BEGIN read01\nKERNEL PANIC: x\n")
        if rows != [("getpid01", "PASS"), ("fork01", "TCONF"),
                    ("read01", "PANIC")] or unreached != ["write01"]:
            print(f"panic inside a test misparsed: {rows} {unreached}",
                  file=sys.stderr)
            return 1
        rows, _ = parse(tmp, "ltp-arm64-0.log", tags[:2],
                        "LTP_BEGIN getpid01\n")
        if rows != [("getpid01", "HANG")]:
            print(f"stall inside a test misparsed: {rows}", file=sys.stderr)
            return 1
        rows, unreached = parse(
            tmp, "ltp-arm64-1.log", tags[:2],
            "LTP_BEGIN getpid01\nLTP_RESULT getpid01 PASS exit=0\n")
        if rows != [("getpid01", "PASS"), ("fork01", "HANG")] or unreached:
            print(f"stall between tests misparsed: {rows} {unreached}",
                  file=sys.stderr)
            return 1
        rows, unreached = parse(
            tmp, "ltp-arm64-2.log", tags[:1],
            "LTP_BEGIN getpid01\nLTP_RESULT getpid01 FAIL exit=1\n"
            "LTP_RUN_DONE total=1\n")
        if rows != [("getpid01", "FAIL")] or unreached:
            print(f"complete run misparsed: {rows} {unreached}",
                  file=sys.stderr)
            return 1
    print("ltp harness contract ok")
    return 0


if __name__ == "__main__":
    sys.exit(main())
