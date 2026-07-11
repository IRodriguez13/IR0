#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host runner for KTM serial protocol (KTM|… / KTM_SUITE_OK)."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    ap = argparse.ArgumentParser(description="Run IR0 KTM scenario via QEMU")
    ap.add_argument("--scenario", default="process.lifecycle")
    ap.add_argument("--log", default="/tmp/ktm-run.log")
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()

    iso = ROOT / "kernel-x64-userspace.iso"
    if not iso.is_file():
        print(f"✗ missing {iso}; build kernel-x64-userspace.iso first", file=sys.stderr)
        return 2

    disk = ROOT / "disk.img"
    if not disk.is_file():
        print("✗ missing disk.img", file=sys.stderr)
        return 2

    log = Path(args.log)
    if log.exists():
        log.unlink()

    smoke = ROOT / "scripts" / "smoke_qemu_run.sh"
    qemu = os.environ.get("QEMU", "qemu-system-x86_64")
    cmd = [
        str(smoke),
        "--log",
        str(log),
        "--timeout",
        str(args.timeout),
        "--success-mode",
        "any",
        "--done",
        "KTM_SUITE_OK",
        "--done",
        "KTM_SUITE_FAIL",
        "--",
        qemu,
        "-cdrom",
        str(iso),
        "-drive",
        f"file={disk},format=raw,if=ide,index=0",
        "-serial",
        "stdio",
        "-display",
        "none",
        "-m",
        "128M",
        "-no-reboot",
        "-net",
        "none",
    ]
    print(f"  KTM-RUN scenario={args.scenario} log={log}")
    rc = subprocess.call(cmd, cwd=str(ROOT))
    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    # Strip NULs from serial
    text = text.replace("\0", "")
    if "KTM_SUITE_OK" not in text and "SUITE_END|pass=" not in text.replace("\n", ""):
        print("✗ KTM suite tag missing", file=sys.stderr)
        for line in text.splitlines():
            if "KTM|" in line or "KTM_" in line:
                print(line)
        return 1 if rc == 0 else rc
    flat = text.replace("\r", "").replace("\n", "")
    required = (
        "TEST_END|process.lifecycle|PASS",
        "TEST_END|ipc.pipe_lifecycle|PASS",
        "TEST_END|mm.cow_fork|PASS",
        "TEST_END|process.exec|PASS",
        "TEST_END|process.fork_rollback|PASS",
    )
    for tag in required:
        if tag not in flat:
            print(f"✗ missing {tag}", file=sys.stderr)
            for line in text.splitlines():
                if "KTM|" in line or "TEST_END" in line:
                    print(line)
            return 1
    print("✓ ktm-run OK (boot suite)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
