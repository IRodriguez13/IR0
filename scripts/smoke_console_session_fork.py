#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Validate console fork+wait: after ash `exit`, getty re-prompts without
respawning the runit service (no second RUNSV_CONSOLE_START).
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISD = ROOT.parent / "ISD"
DEFAULT_TIMEOUT = 75
MONITOR_PORT = 4560


def monitor_send(port: int, cmd: str) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.settimeout(2)
        try:
            sock.recv(4096)
        except socket.timeout:
            pass
        sock.sendall((cmd.strip() + "\r\n").encode("ascii"))
        time.sleep(0.05)
        try:
            sock.recv(4096)
        except socket.timeout:
            pass


def send_keys(port: int, keys: list[str], delay: float = 0.1) -> None:
    for k in keys:
        monitor_send(port, f"sendkey {k}")
        time.sleep(delay)


def log_text(path: Path) -> str:
    try:
        return path.read_text(errors="replace")
    except OSError:
        return ""


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    ap.add_argument(
        "--disk-src",
        default=str(ISD / "out/x86_64/images/development/disk.img"),
    )
    ap.add_argument(
        "--console-bin",
        default=str(ISD / "out/x86_64/product/stage-bin/runit_console_run"),
    )
    ap.add_argument("--log", default="/tmp/console-session-fork-smoke.log")
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    ap.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    args = ap.parse_args()

    console = Path(args.console_bin)
    if not console.is_file():
        print(f"✗ missing {console}", file=sys.stderr)
        return 2

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)

    disk = Path(tempfile.mktemp(prefix="ir0-console-fork.", suffix=".img"))
    shutil.copy2(args.disk_src, disk)

    inj = subprocess.run(
        [
            str(ISD / "scripts/inject-smoke-service.sh"),
            "--run-only",
            str(disk),
            "console",
            str(console),
        ],
        check=False,
    )
    if inj.returncode != 0:
        print("✗ inject console failed", file=sys.stderr)
        disk.unlink(missing_ok=True)
        return 2

    mon = args.monitor_port
    qemu_cmd = [
        "qemu-system-x86_64",
        "-cdrom",
        args.iso,
        "-drive",
        f"file={disk},format=raw,if=ide,index=0",
        "-serial",
        f"file:{log_path}",
        "-display",
        "none",
        "-m",
        "256M",
        "-no-reboot",
        "-net",
        "none",
        "-monitor",
        f"tcp:127.0.0.1:{mon},server,nowait",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )

    def kill_qemu() -> None:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    exited = False
    t0 = time.time()
    try:
        while time.time() - t0 < args.timeout:
            text = log_text(log_path)
            if re.search(r"KERNEL PANIC", text):
                print("FAIL: panic")
                kill_qemu()
                return 1
            if not exited and "ASH_INTERACTIVE_READY" in text:
                time.sleep(1.0)
                send_keys(mon, ["e", "x", "i", "t", "ret"])
                exited = True
            if exited and "CONSOLE_SESSION_REPROMPT" in text:
                # Success criteria
                starts = text.count("RUNSV_CONSOLE_START")
                if starts != 1:
                    print(f"FAIL: RUNSV_CONSOLE_START count={starts} (want 1)")
                    kill_qemu()
                    return 1
                if "CONSOLE_SESSION_START" not in text:
                    print("FAIL: missing CONSOLE_SESSION_START")
                    kill_qemu()
                    return 1
                if "CONSOLE_SESSION_END" not in text and "CONSOLE_SESSION_SEGV" not in text:
                    print("FAIL: missing CONSOLE_SESSION_END/SEGV")
                    kill_qemu()
                    return 1
                print("PASS: session ended; console re-prompted without service restart")
                print(f"log: {log_path}")
                kill_qemu()
                return 0
            time.sleep(0.25)
    finally:
        kill_qemu()
        disk.unlink(missing_ok=True)

    text = log_text(log_path)
    print("FAIL: timeout")
    for line in text.splitlines():
        if any(
            s in line
            for s in (
                "RUNSV_CONSOLE",
                "CONSOLE_SESSION",
                "ASH_INTERACTIVE",
                "LOGIN_",
                "GETTY",
            )
        ):
            print(line)
    return 1


if __name__ == "__main__":
    sys.exit(main())
