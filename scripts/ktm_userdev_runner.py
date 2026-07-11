#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host runner for KTM /dev/ktm userspace pilot (KTM_USERDEV_OK)."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    ap = argparse.ArgumentParser(description="Run IR0 KTM userdev pilot via QEMU")
    ap.add_argument("--log", default="/tmp/ktm-userdev-run.log")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--init", default=str(ROOT / "userspace/libktm/ktm_fork_wait_case"))
    args = ap.parse_args()

    iso = ROOT / "kernel-x64-userspace.iso"
    if not iso.is_file():
        print(f"✗ missing {iso}; build kernel-x64-userspace.iso first", file=sys.stderr)
        return 2

    init_bin = Path(args.init)
    if not init_bin.is_file():
        print(f"✗ missing init {init_bin}; build first", file=sys.stderr)
        return 2

    base = ROOT / "disk.img"
    if not base.is_file():
        print("✗ missing disk.img", file=sys.stderr)
        return 2

    log = Path(args.log)
    if log.exists():
        log.unlink()

    disk = Path(tempfile.mktemp(prefix="ir0-ktm-userdev-", suffix=".img"))
    try:
        subprocess.check_call(["cp", "-f", str(base), str(disk)], cwd=str(ROOT))
        subprocess.check_call(
            ["python3", "scripts/inject_init_minix.py", str(disk), str(init_bin), "sbin/init"],
            cwd=str(ROOT),
        )

        smoke = ROOT / "scripts" / "smoke_qemu_run.sh"
        qemu = os.environ.get("QEMU", "qemu-system-x86_64")
        cmd = [
            str(smoke),
            "--log",
            str(log),
            "--timeout",
            str(args.timeout),
            "--done",
            "KTM_USERDEV_OK",
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
        print(f"  KTM-USERDEV-RUN log={log}")
        rc = subprocess.call(cmd, cwd=str(ROOT))
    finally:
        if disk.exists():
            disk.unlink()

    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    text = text.replace("\0", "").replace("\r", "")
    flat = text.replace("\n", "")
    if "KTM_USERDEV_OK" not in flat:
        print("✗ KTM_USERDEV_OK missing", file=sys.stderr)
        for line in text.splitlines():
            if "KTM|" in line or "KTM_" in line:
                print(line)
        return 1 if rc == 0 else rc
    if "TEST_END|fork_wait_signal|PASS" not in flat:
        print("✗ fork_wait_signal did not PASS", file=sys.stderr)
        return 1
    print("✓ ktm-userdev-run OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
