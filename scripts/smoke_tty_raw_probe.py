#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
TTY raw probe smoke: HMP injects Ctrl-X + Up-arrow into tty_raw_probe.

Validates keyboard Ctrl map (0x18) and CSI arrow path without nano.
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
sys.path.insert(0, str(ROOT / "scripts"))
from smoke_desktop_cmd_matrix import Monitor, cleanup_port  # noqa: E402

INJECT = ROOT / "scripts" / "inject_init_minix.py"
PROBE_SRC = ROOT / "setup" / "pid1" / "tty_raw_probe.c"
PROBE_BIN = ROOT / "setup" / "pid1" / "tty_raw_probe"


def build_probe() -> None:
    musl = os.environ.get("MUSL_CC") or shutil.which("x86_64-linux-musl-gcc") or shutil.which(
        "musl-gcc"
    )
    if not musl:
        raise SystemExit("✗ musl-gcc required to build tty_raw_probe")
    subprocess.run(
        [musl, "-static", "-Os", "-o", str(PROBE_BIN), str(PROBE_SRC)],
        check=True,
        cwd=str(ROOT),
    )


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-tty-raw-probe.log"))
    ap.add_argument("--port", type=int, default=46111)
    ap.add_argument("--timeout", type=float, default=90.0)
    ap.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 2

    build_probe()
    work = Path(tempfile.mktemp(suffix=".img", prefix="ir0-tty-probe."))
    shutil.copy2(args.disk, work)
    subprocess.run(
        [sys.executable, str(INJECT), str(work), str(PROBE_BIN), "usr/bin/tty_raw_probe"],
        check=True,
        cwd=str(ROOT),
    )

    args.log.unlink(missing_ok=True)
    cleanup_port(args.port)
    mon = Monitor(args.port)
    proc = subprocess.Popen(
        [
            args.qemu,
            "-cdrom",
            str(args.iso),
            "-drive",
            f"file={work},format=raw,if=ide,index=0",
            "-serial",
            f"file:{args.log}",
            "-display",
            "none",
            "-m",
            "256M",
            "-no-reboot",
            "-net",
            "none",
            "-monitor",
            f"tcp:127.0.0.1:{args.port},server,nowait",
        ],
        cwd=str(ROOT),
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    t0 = time.monotonic()
    try:
        mon.connect(timeout=25)
        while time.monotonic() - t0 < args.timeout:
            text = args.log.read_text(errors="replace") if args.log.is_file() else ""
            if "ASH_INTERACTIVE_READY" in text or "root@" in text:
                break
            if (
                "login:" in text.lower()
                and "ASH_INTERACTIVE_READY" not in text
                and "LOGIN_USER_READ" not in text[-800:]
            ):
                # Prefer ivan (desktop may deny root via /etc/ir0-noroot).
                time.sleep(0.5)
                user = "ivan" if "LOGIN_ROOT_DENIED" in text else "root"
                mon.type_str(user, 0.40)
                mon.ret()
                time.sleep(0.8)
                mon.ret()  # empty password
                time.sleep(2.0)
                continue
            if proc.poll() is not None:
                break
            time.sleep(0.3)
        else:
            print("✗ timeout waiting for shell", file=sys.stderr)
            print((args.log.read_text(errors="replace") if args.log.is_file() else "")[-1500:])
            return 1

        time.sleep(0.8)
        mon.type_str("/usr/bin/tty_raw_probe", 0.35)
        mon.ret()
        time.sleep(1.0)
        # Wait for probe ready tag
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            text = args.log.read_text(errors="replace")
            if "TTY_PROBE_WAIT_CTRLX" in text:
                break
            time.sleep(0.2)
        mon.key("ctrl-x")
        time.sleep(0.8)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            text = args.log.read_text(errors="replace")
            if "TTY_PROBE_WAIT_ARROW" in text:
                break
            time.sleep(0.2)
        mon.key("up")
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            text = args.log.read_text(errors="replace")
            if "TTY_PROBE_WAIT_ENTER" in text:
                break
            time.sleep(0.2)
        mon.key("ret")
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            text = args.log.read_text(errors="replace")
            if "TTY_PROBE_OK" in text:
                print("✓ smoke-tty-raw-probe PASS (Ctrl-X=0x18 + ESC[A] + Enter=0x0d)")
                return 0
            if "TTY_PROBE_FAIL" in text:
                print("✗ probe reported FAIL", file=sys.stderr)
                print(text[-2000:], file=sys.stderr)
                return 1
            time.sleep(0.2)
        print("✗ timeout waiting TTY_PROBE_OK", file=sys.stderr)
        print(args.log.read_text(errors="replace")[-2500:], file=sys.stderr)
        return 1
    finally:
        mon.close()
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
        work.unlink(missing_ok=True)


if __name__ == "__main__":
    raise SystemExit(main())
