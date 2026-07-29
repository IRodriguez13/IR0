#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Reproduce ash SEGV without getty/login.

Replaces console/run with ash_direct_console_run (interactive -sh -i on
/dev/console), injects keystrokes via QEMU HMP (Tab / ulimit), and greps
serial for userspace segv / probe tags.
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
DEFAULT_TIMEOUT = 70
MONITOR_PORT = 4555

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE PANIC"),
]


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


def send_keys(port: int, keys: list[str]) -> None:
    for k in keys:
        monitor_send(port, f"sendkey {k}")
        time.sleep(0.08)


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
    ap.add_argument("--log", default="/tmp/ash-direct-segv-probe.log")
    ap.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    ap.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    args = ap.parse_args()

    smoke_bin = ISD / "out/x86_64/smoke/stage-bin"
    direct = smoke_bin / "ash_direct_console_run"
    if not direct.is_file():
        print(f"✗ missing {direct} — build ISD smoke first", file=sys.stderr)
        return 2

    log_path = Path(args.log)
    if log_path.exists():
        log_path.unlink()

    disk = Path(tempfile.mktemp(prefix="ir0-ash-direct.", suffix=".img"))
    shutil.copy2(args.disk_src, disk)

    inj = subprocess.run(
        [
            str(ISD / "scripts/inject-smoke-service.sh"),
            "--run-only",
            str(disk),
            "console",
            str(direct),
        ],
        check=False,
    )
    if inj.returncode != 0:
        print("✗ inject console→ash_direct failed", file=sys.stderr)
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
        "stdio",
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

    env = os.environ.copy()
    with log_path.open("w") as logf:
        proc = subprocess.Popen(
            qemu_cmd,
            stdin=subprocess.DEVNULL,
            stdout=logf,
            stderr=subprocess.STDOUT,
            env=env,
        )

    def kill_qemu(*_a: object) -> None:
        if proc.poll() is None:
            proc.send_signal(signal.SIGTERM)
            try:
                proc.wait(timeout=3)
            except subprocess.TimeoutExpired:
                proc.kill()

    keyed = False
    t0 = time.time()
    try:
        while time.time() - t0 < args.timeout:
            text = log_text(log_path)
            for fr in FAIL_RES:
                if fr.search(text):
                    print("FAIL: panic in log")
                    kill_qemu()
                    return 1
            if "GETTY_READY" in text or "LOGIN_USER_READ" in text:
                print("FAIL: getty/login ran (console replace ineffective)")
                kill_qemu()
                return 1
            if not keyed and "ASH_DIRECT_START" in text:
                # Give ash a moment to print prompt
                time.sleep(1.5)
                # Guest listed PATH via Tab; also force a large /bin listing.
                for _ in range(4):
                    send_keys(mon, ["tab"])
                    time.sleep(0.4)
                send_keys(
                    mon,
                    [
                        "l",
                        "s",
                        "spc",
                        "slash",
                        "b",
                        "i",
                        "n",
                        "ret",
                    ],
                )
                time.sleep(1.5)
                send_keys(
                    mon,
                    ["u", "l", "i", "m", "i", "t", "ret"],
                )
                time.sleep(0.8)
                send_keys(
                    mon,
                    [
                        "e",
                        "x",
                        "e",
                        "c",
                        "spc",
                        "minus",
                        "minus",
                        "v",
                        "e",
                        "r",
                        "s",
                        "i",
                        "o",
                        "n",
                        "ret",
                    ],
                )
                time.sleep(0.5)
                # Extra stress: ulimit -a then another Tab
                send_keys(
                    mon,
                    ["u", "l", "i", "m", "i", "t", "spc", "minus", "a", "ret"],
                )
                time.sleep(0.5)
                send_keys(mon, ["tab", "tab"])
                keyed = True
                time.sleep(3.0)
            if keyed and "userspace segv" in text:
                print("RESULT: SEGV reproduced (interactive ash, no getty)")
                kill_qemu()
                print(f"log: {log_path}")
                return 0
            if keyed and time.time() - t0 > 40:
                break
            time.sleep(0.25)
    finally:
        kill_qemu()
        disk.unlink(missing_ok=True)

    text = log_text(log_path)
    print("=== tags / PF (tail) ===")
    for line in text.splitlines():
        if any(
            s in line
            for s in (
                "ASH_DIRECT",
                "userspace segv",
                "GETTY",
                "LOGIN_",
                "unlimited",
                "illegal option",
            )
        ):
            print(line)

    if "userspace segv" in text:
        print("RESULT: SEGV reproduced")
        return 0
    if "ASH_DIRECT_START" not in text:
        print("RESULT: ash direct never started")
        return 1
    print("RESULT: no SEGV after Tab+ulimit+exec (interactive, no getty)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
