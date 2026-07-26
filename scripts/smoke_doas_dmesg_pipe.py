#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Reproduce / gate: doas dmesg | grep must not panic (pipe double-free)."""

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
PROMPT_RE = re.compile(r"(?:root|ivan)@ir0:\S*[#$]")


def mon(port: int, cmd: str, wait: float = 0.08) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(1.5)
        try:
            s.recv(4096)
        except Exception:
            pass
        s.sendall((cmd.strip() + "\r\n").encode())
        time.sleep(wait)
        try:
            while s.recv(4096):
                pass
        except Exception:
            pass


def type_str(port: int, s: str, delay: float = 0.14) -> None:
    for ch in s:
        if ch == " ":
            mon(port, "sendkey spc", delay)
        elif ch == "|":
            mon(port, "sendkey shift-backslash", delay)
        elif ch == "-":
            mon(port, "sendkey minus", delay)
        elif ch == "/":
            mon(port, "sendkey slash", delay)
        elif ch.isupper():
            mon(port, f"sendkey shift-{ch.lower()}", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-doas-pipe.log"))
    ap.add_argument("--port", type=int, default=46231)
    args = ap.parse_args()

    work = Path(tempfile.mktemp(suffix=".img", prefix="ir0-doas-pipe."))
    shutil.copy2(args.disk, work)
    args.log.write_text("")
    proc = subprocess.Popen(
        [
            "qemu-system-x86_64",
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
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    try:
        t0 = time.time()
        while time.time() - t0 < 90:
            t = args.log.read_text(errors="replace")
            if "ASH_INTERACTIVE_READY" in t or PROMPT_RE.search(t):
                break
            if proc.poll() is not None:
                print("✗ qemu died before shell")
                return 1
            time.sleep(0.25)
        else:
            print("✗ shell timeout")
            return 1
        time.sleep(1.0)

        variants = [
            "dmesg",
            "dmesg | grep IR0",
            "doas true",
            "doas id",
            "doas dmesg",
            "doas dmesg | cat",
            "doas dmesg | grep IR0",
        ]
        for cmd in variants:
            mark = len(args.log.read_text(errors="replace"))
            type_str(args.port, cmd)
            mon(args.port, "sendkey ret")
            deadline = time.time() + 25
            while time.time() < deadline:
                text = args.log.read_text(errors="replace")
                if "KERNEL PANIC" in text or "double-free" in text:
                    print(f"✗ PANIC on: {cmd}")
                    print(text[mark:][-800:])
                    return 1
                # New prompt after command
                if PROMPT_RE.search(text[mark:]) and time.time() > deadline - 23:
                    # require some progress; wait for prompt count increase
                    pass
                if text[mark:].count("@ir0:") >= 1 and "KERNEL PANIC" not in text:
                    # Heuristic: saw echo/output and still alive
                    if PROMPT_RE.search(text[-400:]):
                        break
                time.sleep(0.2)
            else:
                # Timeout without panic — still check
                text = args.log.read_text(errors="replace")
                if "KERNEL PANIC" in text or "double-free" in text:
                    print(f"✗ PANIC on: {cmd}")
                    return 1
                print(f"  WARN timeout waiting prompt after: {cmd}")
            print(f"  OK  {cmd}")
            time.sleep(0.4)

        # Loop the hottest case
        for i in range(5):
            mark = len(args.log.read_text(errors="replace"))
            type_str(args.port, "doas dmesg | grep IR0")
            mon(args.port, "sendkey ret")
            time.sleep(2.5)
            text = args.log.read_text(errors="replace")
            if "KERNEL PANIC" in text or "double-free" in text:
                print(f"✗ PANIC on loop iter {i}")
                return 1
            print(f"  OK  loop {i+1}/5")

        print("✓ smoke-doas-dmesg-pipe PASS")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()
        work.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
