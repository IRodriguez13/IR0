#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Recovery fsck + remount RW smoke.

Injects a one-shot helper on a temp disk copy. Recovery login profile runs
/sbin/rfsmoke when /etc/ir0-recovery-rfsmoke is present (smoke-only wiring).

Usage:
  make smoke-fsck-remount-rw
"""

from __future__ import annotations

import argparse
import importlib.util
import os
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISD = Path(os.environ.get("IR0_ISD_ROOT", str(ROOT.parent / "ISD")))

_spec = importlib.util.spec_from_file_location(
    "relogin", str(ROOT / "scripts" / "smoke_desktop_relogin.py"))
relogin = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(relogin)

read_log = relogin.read_log
kill_qemu = relogin.kill_qemu
wait_tags = relogin.wait_tags


def profile_with_recovery_hook() -> Path:
    for candidate in (
        ISD / "rootfs" / "etc" / "profile",
        ISD / "rootfs" / "base" / "etc" / "profile",
    ):
        if candidate.is_file():
            base = candidate.read_text(encoding="utf-8")
            break
    else:
        base = "export PATH=/bin:/sbin:/usr/bin:/usr/sbin\n"
    hook = (
        "\n# smoke-fsck-remount-rw (temp disk inject only)\n"
        "if [ -f /etc/rfsmoke.flag ] && [ -x /sbin/rfsmoke ]; then\n"
        "\t/sbin/rfsmoke\n"
        "fi\n"
    )
    tmp = Path(tempfile.mktemp(prefix="ir0-recovery-profile.", suffix=".sh"))
    tmp.write_text(base + hook, encoding="utf-8")
    return tmp


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace-recovery.iso"))
    ap.add_argument("--disk", default=str(ROOT / "disk.img"))
    ap.add_argument("--log", default="/tmp/ir0-fsck-remount.log")
    ap.add_argument("--timeout", type=int, default=90)
    args = ap.parse_args()

    iso = Path(args.iso)
    src = Path(args.disk)
    log_path = Path(args.log)
    if not iso.is_file() or not src.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 1

    helper = ROOT / "scripts" / "recovery_fsck_smoke.sh"
    marker = Path(tempfile.mktemp(prefix="ir0-recovery-marker.", suffix=".txt"))
    marker.write_text("1\n", encoding="utf-8")
    profile = profile_with_recovery_hook()
    disk = Path(tempfile.mktemp(prefix="ir0-fsck-remount.", suffix=".img"))
    inject = ROOT / "scripts" / "inject_init_minix.py"
    proc = None
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        for mode, src_path, dest in (
            ("0755", helper, "sbin/rfsmoke"),
            ("0644", marker, "etc/rfsmoke.flag"),
            ("0644", profile, "etc/profile"),
        ):
            subprocess.run(
                [sys.executable, str(inject), "--mode", mode, str(disk),
                 str(src_path), dest],
                check=True,
            )

        log_path.unlink(missing_ok=True)
        proc = subprocess.Popen(
            [
                os.environ.get("QEMU", "qemu-system-x86_64"),
                "-cdrom", str(iso),
                "-drive", f"file={disk},format=raw,if=ide,index=0",
                "-serial", f"file:{log_path}",
                "-display", "none",
                "-m", "256M",
                "-no-reboot",
                "-net", "none",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

        if not wait_tags(log_path, ["RECOVERY_SHELL_READY"], proc, float(args.timeout)):
            kill_qemu(proc)
            print("✗ recovery shell not ready", file=sys.stderr)
            print(read_log(log_path)[-6000:], file=sys.stderr)
            return 1

        deadline = time.time() + 45
        while time.time() < deadline:
            text = read_log(log_path)
            if "FSCK_FAIL" in text or "RECOVERY_REMOUNT_RW_FAIL" in text:
                kill_qemu(proc)
                print("✗ fsck or remount failed", file=sys.stderr)
                print(text[-4000:], file=sys.stderr)
                return 1
            if ("RECOVERY_FSCK_REMOUNT_DONE" in text and "RECOVERY_ROOT_RW" in text
                    and "FSCK_FAIL" not in text
                    and ("FSCK_OK" in text or "FSCK_SKIPPED" in text)):
                kill_qemu(proc)
                print("✓ smoke-fsck-remount-rw OK (fsck honest + RECOVERY_ROOT_RW)", flush=True)
                return 0
            if proc.poll() is not None:
                break
            time.sleep(0.3)

        kill_qemu(proc)
        print("✗ missing FSCK_OK / RECOVERY_ROOT_RW / RECOVERY_FSCK_REMOUNT_DONE",
              file=sys.stderr)
        print(read_log(log_path)[-5000:], file=sys.stderr)
        return 1
    finally:
        if proc is not None and proc.poll() is None:
            kill_qemu(proc)
        disk.unlink(missing_ok=True)
        marker.unlink(missing_ok=True)
        profile.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
