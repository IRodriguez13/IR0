#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guest smoke: date + /proc coherence tags (HMP sendkey)."""

from __future__ import annotations

import re
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PROMPT_RE = re.compile(r"(?:ivan|root)@(?:ir0|unix):\S*[#$]")


def mon(port: int, cmd: str, delay: float = 0.05) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.sendall((cmd + "\n").encode())
        time.sleep(delay)


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
            mon(port, f"sendkey {special[ch]}")
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}")
        else:
            mon(port, f"sendkey {ch}")
    mon(port, "sendkey ret", 0.2)


def main() -> int:
    port = 4444
    log = Path(tempfile.mkstemp(prefix="ir0-proc-coh-", suffix=".log")[1])
    disk = ROOT / "disk.img"
    iso = ROOT / "kernel-x64-userspace.iso"
    if not disk.is_file() or not iso.is_file():
        print("✗ need disk.img and kernel-x64-userspace.iso", file=sys.stderr)
        return 2

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
        "-rtc",
        "base=utc",
    ]
    with log.open("w") as lf:
        proc = subprocess.Popen(
            qemu, stdout=lf, stderr=subprocess.STDOUT, cwd=str(ROOT)
        )
    try:
        deadline = time.time() + 45
        while time.time() < deadline:
            text = log.read_text(errors="replace")
            if PROMPT_RE.search(text) or "GETTY_READY" in text:
                break
            if proc.poll() is not None:
                print("✗ qemu exited early", file=sys.stderr)
                return 1
            time.sleep(0.3)
        time.sleep(1.0)
        # Drain monitor banner
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
                s.settimeout(0.5)
                try:
                    s.recv(4096)
                except OSError:
                    pass
        except OSError:
            print("✗ monitor connect failed", file=sys.stderr)
            return 1

        for cmd in (
            "date",
            "cat /proc/uptime",
            "cat /proc/loadavg",
            "cat /proc/stat",
            "cat /proc/meminfo",
            "cat /proc/iomem",
            "echo PROC_COH_DONE",
        ):
            type_line(port, cmd)
            time.sleep(0.8)

        time.sleep(1.5)
        text = log.read_text(errors="replace")
        ok = True
        # date should not be 1970 when RTC is wired
        if re.search(r"\b1970\b", text) and "Jan" in text:
            print("✗ date still shows 1970", file=sys.stderr)
            ok = False
        if not re.search(r"\d+\.\d{2}\s+\d+\.\d{2}", text):
            print("✗ /proc/uptime format missing", file=sys.stderr)
            ok = False
        if not re.search(r"\d+\.\d{2}\s+\d+\.\d{2}\s+\d+\.\d{2}\s+\d+/\d+\s+\d+", text):
            print("✗ /proc/loadavg format missing", file=sys.stderr)
            ok = False
        # BusyBox top needs "cpu" + ≥4 jiffy fields (user nice system idle …)
        if not re.search(r"(?m)^cpu\s+\d+\s+\d+\s+\d+\s+\d+", text):
            print("✗ /proc/stat cpu line missing", file=sys.stderr)
            ok = False
        if not re.search(r"(?m)^cpu0\s+\d+\s+\d+\s+\d+\s+\d+", text):
            print("✗ /proc/stat cpu0 line missing", file=sys.stderr)
            ok = False
        if "MemTotal:" not in text or "kB" not in text:
            print("✗ /proc/meminfo labels missing", file=sys.stderr)
            ok = False
        if "System RAM (PMM-managed)" not in text:
            print("✗ /proc/iomem PMM line missing", file=sys.stderr)
            ok = False
        if "PROC_COH_DONE" not in text:
            print("✗ marker missing — serial dump:", file=sys.stderr)
            print(text[-4000:], file=sys.stderr)
            ok = False
        if ok:
            print("✓ smoke-proc-coherence OK")
            print("--- serial excerpt ---")
            for line in text.splitlines():
                if any(
                    x in line
                    for x in (
                        "MemTotal",
                        "MemFree",
                        "1970",
                        "202",
                        "System RAM",
                        "PROC_COH",
                        "cpu ",
                        "cpu0",
                        ".",
                    )
                ):
                    if (
                        "Mem" in line
                        or "RAM" in line
                        or "PROC" in line
                        or line.startswith("cpu")
                        or re.search(r"\d+\.\d{2}", line)
                    ):
                        print(line)
            return 0
        print(text[-6000:], file=sys.stderr)
        return 1
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
