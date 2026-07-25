#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Non-root Unix login smoke: crypt(3) auth as ivan, then privilege drop.

Covers what smoke_runit_login.py (root, empty shadow field) cannot: SHA-512
crypt verification, setgroups/setgid/setuid to uid 1000, chdir to $HOME, and
the login shell sourcing /etc/profile to build the non-root PS1.

The temp disk gets an /etc/shadow whose ivan entry is a real SHA-512 crypt hash
of the empty password, so the console autologin path exercises crypt(3) without
depending on typed input: the PS/2 canon-line wake is still unreliable (same
gap as the residual ASH_COMMAND_ECHO_OK failure in smoke-runit-ash-interactive),
so keystroke-driven login is validated manually, not here.
"""

from __future__ import annotations

import argparse
import os
import shutil
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TIMEOUT = 75

LOGIN_USER = "ivan"
EXPECTED_UID_TAG = "LOGIN_UID=1000 EUID=1000"
EXPECTED_PROMPT = "ivan@ir0:/home/ivan$"

# crypt(3) SHA-512 hash of the empty password (crypt("", "$6$ir0empty12345678")).
IVAN_EMPTY_SHA512 = (
    "$6$ir0empty12345678$hbseqGvZwDGnvGJ4m23u22ArU9iDrblAyBKdh9bmC9vZa8yd1UdPJ"
    "PX93kKsc2DOnOxBXCjjxwCkR8/4XDsHU."
)
SHADOW_CONTENT = (
    "root::0:0:99999:7:::\n"
    f"ivan:{IVAN_EMPTY_SHA512}:0:0:99999:7:::\n"
)

FAIL_MARKERS = [
    "KERNEL PANIC",
    "RUNSV_CONSOLE_EXEC_FAIL",
    "LOGIN_AUTO_FAIL",
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


def inject(disk: Path, content: str, dest: str) -> None:
    tmp = Path(tempfile.mktemp(prefix="ir0-login-nonroot-inject."))
    tmp.write_text(content)
    try:
        subprocess.run(
            [
                "python3",
                str(ROOT / "scripts" / "inject_init_minix.py"),
                str(disk),
                str(tmp),
                dest,
            ],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    finally:
        tmp.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Runit non-root login smoke")
    parser.add_argument("--log", default="/tmp/runit-login-nonroot-smoke.log")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    parser.add_argument("--disk", default=str(ROOT / "disk.img"))
    args = parser.parse_args()

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)
    iso = Path(args.iso)
    src_disk = Path(args.disk)
    if not iso.is_file() or not src_disk.is_file():
        print("✗ missing iso/disk — make load-userspace-runit + kernel-x64-userspace.iso",
              file=sys.stderr)
        return 1

    disk = Path(tempfile.mktemp(prefix="ir0-login-nonroot.", suffix=".img"))
    shutil.copy2(src_disk, disk)
    inject(disk, f"{LOGIN_USER}\n", "etc/ir0-autologin")
    inject(disk, SHADOW_CONTENT, "etc/shadow")

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
        start_new_session=True,
    )

    start = time.monotonic()
    deadline = start + args.timeout
    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break
            text = read_log(log_path)

            for marker in FAIL_MARKERS:
                if marker in text:
                    print(f"✗ smoke-runit-login-nonroot FAIL: {marker}")
                    print(f"  log: {log_path}")
                    return 1

            if EXPECTED_UID_TAG in text and EXPECTED_PROMPT in text:
                elapsed = time.monotonic() - start
                kill_qemu(proc)
                print(f"✓ smoke-runit-login-nonroot PASS "
                      f"(crypt auth + uid 1000 + PS1 {EXPECTED_PROMPT!r}, {elapsed:.1f}s)")
                return 0

            time.sleep(0.2)

        text = read_log(log_path)
        print("✗ smoke-runit-login-nonroot FAIL (timeout)")
        if "LOGIN_OK" not in text:
            print("  - crypt(3) auth never succeeded for ivan")
        if EXPECTED_UID_TAG not in text:
            print(f"  - missing uid tag: {EXPECTED_UID_TAG} (privilege drop)")
        if EXPECTED_PROMPT not in text:
            print(f"  - missing prompt: {EXPECTED_PROMPT} (/etc/profile PS1)")
        print(f"  log: {log_path}")
        return 1
    finally:
        kill_qemu(proc)
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
