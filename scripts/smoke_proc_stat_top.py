#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guest smoke: /proc/stat present and BusyBox top -bn1 does not die on it."""

from __future__ import annotations

import argparse
import re
import shutil
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ISD = ROOT.parent / "ISD"
PROMPT_RE = re.compile(r"root@ir0:[^\s#$]+[#$]")


def mon(port: int, cmd: str, delay: float = 0.05) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(0.5)
        try:
            s.recv(4096)
        except OSError:
            pass
        s.sendall((cmd + "\n").encode())
        time.sleep(delay)
        try:
            s.recv(4096)
        except OSError:
            pass


def type_line(port: int, line: str) -> None:
    special = {
        " ": "spc",
        "/": "slash",
        "-": "minus",
        ".": "dot",
        "_": "shift-minus",
        ":": "shift-semicolon",
    }
    for ch in line:
        if ch in special:
            mon(port, f"sendkey {special[ch]}", 0.12)
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", 0.12)
        else:
            mon(port, f"sendkey {ch}", 0.12)
    mon(port, "sendkey ret", 0.35)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    ap.add_argument(
        "--disk-src",
        default=str(ISD / "out/x86_64/images/development/disk.img"),
    )
    ap.add_argument("--monitor-port", type=int, default=4564)
    ap.add_argument("--timeout", type=int, default=70)
    args = ap.parse_args()

    iso = Path(args.iso)
    disk_src = Path(args.disk_src)
    if not iso.is_file() or not disk_src.is_file():
        print(f"✗ need iso ({iso}) and disk ({disk_src})", file=sys.stderr)
        return 2

    log = Path(tempfile.mkstemp(prefix="ir0-proc-stat-", suffix=".log")[1])
    disk = Path(tempfile.mktemp(prefix="ir0-proc-stat.", suffix=".img"))
    shutil.copy2(disk_src, disk)
    port = args.monitor_port

    qemu = [
        "qemu-system-x86_64",
        "-cdrom",
        str(iso),
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
        f"tcp:127.0.0.1:{port},server,nowait",
    ]
    with log.open("w") as lf:
        proc = subprocess.Popen(
            qemu, stdout=lf, stderr=subprocess.STDOUT, cwd=str(ROOT)
        )
    try:
        deadline = time.time() + args.timeout
        text = ""
        while time.time() < deadline:
            text = log.read_text(errors="replace")
            if PROMPT_RE.search(text) and "ASH_INTERACTIVE_READY" in text:
                break
            if proc.poll() is not None:
                print("✗ qemu exited early", file=sys.stderr)
                print(text[-3000:], file=sys.stderr)
                return 1
            time.sleep(0.3)
        else:
            print("✗ timeout waiting for root shell", file=sys.stderr)
            print(text[-3000:], file=sys.stderr)
            return 1

        # Autologin shell is ready; give the console a beat before HMP sendkey.
        time.sleep(3.0)
        for cmd in ("cat /proc/stat", "echo STAT_OK", "top -bn1", "echo TOP_OK"):
            type_line(port, cmd)
            time.sleep(2.5)

        time.sleep(1.5)
        text = log.read_text(errors="replace")
        ok = True
        if not re.search(r"(?m)^cpu\s+\d+\s+\d+\s+\d+\s+\d+", text):
            print("✗ /proc/stat cpu line missing", file=sys.stderr)
            ok = False
        if not re.search(r"(?m)^cpu0\s+\d+\s+\d+\s+\d+\s+\d+", text):
            print("✗ /proc/stat cpu0 line missing", file=sys.stderr)
            ok = False
        if "STAT_OK" not in text:
            print("✗ STAT_OK marker missing", file=sys.stderr)
            ok = False
        if "can't open 'stat'" in text or "can't read '/proc/stat'" in text:
            print("✗ BusyBox top failed on /proc/stat", file=sys.stderr)
            ok = False
        if "no process info in /proc" in text:
            print("✗ BusyBox top: no process info (getdents PID list)", file=sys.stderr)
            ok = False
        # top -bn1 header proves jiffy parse + process scan (primary BusyBox contract)
        if "Mem:" not in text or "CPU:" not in text:
            print("✗ top -bn1 header missing", file=sys.stderr)
            ok = False
        if "TOP_OK" not in text:
            print("✗ TOP_OK marker missing (top may have aborted)", file=sys.stderr)
            ok = False
        if ok:
            print("✓ smoke-proc-stat-top OK")
            for line in text.splitlines():
                if line.startswith("cpu") or line in ("STAT_OK", "TOP_OK"):
                    print(line)
                if "Mem:" in line or "CPU:" in line or "Load average" in line:
                    print(line)
            return 0
        print(text[-5000:], file=sys.stderr)
        return 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
