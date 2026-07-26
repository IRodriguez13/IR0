#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Headless runit login smoke: GETTY_READY → LOGIN_OK (via /etc/ir0-autologin).

Product interactive path remains unix login: / Password: when the sentinel
is absent (IR0_NO_AUTOLOGIN=1 make load-userspace-runit).
"""

from __future__ import annotations

import argparse
import os
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TIMEOUT = 60

NEED_TAGS = [
    "RUNIT_STAGE1_OK",
    "GETTY_READY",
    "LOGIN_OK",
    "ASH_INTERACTIVE_READY",
    "Welcome to IR0",
]


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def kill_qemu(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass


def main() -> int:
    parser = argparse.ArgumentParser(description="Runit Unix login smoke")
    parser.add_argument("--log", default="/tmp/runit-login-smoke.log")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    parser.add_argument("--disk", default=str(ROOT / "disk.img"))
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)
    iso = Path(args.iso)
    src = Path(args.disk)
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk — make load-userspace-runit + kernel-x64-userspace.iso",
              file=sys.stderr)
        return 1

    disk = Path(tempfile.mktemp(prefix="ir0-login-smoke.", suffix=".img"))
    subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
    proc = subprocess.Popen(
        [
            args.qemu,
            "-cdrom", str(iso),
            "-drive", f"file={disk},format=raw,if=ide,index=0",
            "-serial", f"file:{log_path}",
            "-display", "none",
            "-m", "256M",
            "-no-reboot",
            "-net", "none",
        ],
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    deadline = time.monotonic() + args.timeout
    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break
            text = read_log(log_path)
            if all(t in text for t in NEED_TAGS):
                print("✓ smoke-runit-login PASS (GETTY_READY + LOGIN_OK + welcome)")
                return 0
            time.sleep(0.2)
        text = read_log(log_path)
        print("✗ smoke-runit-login FAIL")
        for t in NEED_TAGS:
            if t not in text:
                print(f"  - missing {t}")
        return 1
    finally:
        kill_qemu(proc)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
