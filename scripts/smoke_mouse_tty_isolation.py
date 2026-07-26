#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""QEMU smoke: PS/2 mouse must not inject TTY bytes; keyboard still works."""

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
KERN_RE = re.compile(r"\[#\d+\][^\n]*\n?")
CSI_RE = re.compile(r"\x1b\[[0-9;?]*[A-Za-z]")


def mon_cmd(port: int, cmd: str, wait: float = 0.02) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=3) as s:
        s.settimeout(0.4)
        try:
            s.recv(4096)
        except Exception:
            pass
        s.sendall((cmd.strip() + "\r\n").encode())
        time.sleep(wait)
        try:
            s.recv(4096)
        except Exception:
            pass


def strip_noise(text: str) -> str:
    text = KERN_RE.sub("", text)
    text = CSI_RE.sub("", text)
    return text


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-mouse-tty.log"))
    ap.add_argument("--port", type=int, default=46221)
    ap.add_argument("--timeout", type=float, default=90.0)
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print("✗ missing iso/disk", file=sys.stderr)
        return 2

    work = Path(tempfile.mktemp(suffix=".img", prefix="ir0-mouse-tty."))
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
        while time.time() - t0 < args.timeout:
            if proc.poll() is not None:
                print("✗ qemu exited early", file=sys.stderr)
                return 1
            text = args.log.read_text(errors="replace")
            if "ASH_INTERACTIVE_READY" in text or PROMPT_RE.search(text):
                break
            time.sleep(0.2)
        else:
            print("✗ no shell prompt", file=sys.stderr)
            return 1

        time.sleep(0.8)
        # Drain monitor banner
        for _ in range(3):
            try:
                mon_cmd(args.port, "info version")
                break
            except OSError:
                time.sleep(0.2)

        mark = len(args.log.read_text(errors="replace"))
        for i in range(25):
            mon_cmd(args.port, f"mouse_move {i % 7} {(i * 3) % 5}")
            if i % 5 == 0:
                mon_cmd(args.port, "mouse_button 1")
                mon_cmd(args.port, "mouse_button 0")
        time.sleep(0.8)

        mid = strip_noise(args.log.read_text(errors="replace")[mark:])
        # Prompt redraws are OK; alphanumeric from mouse path is not.
        mid_noprompt = PROMPT_RE.sub("", mid)
        if re.search(r"[A-Za-z0-9]", mid_noprompt):
            print("✗ mouse produced TTY-visible chars:", repr(mid_noprompt[:240]))
            return 1

        # Keyboard still responds (short path — HMP drops long strings).
        mark2 = len(args.log.read_text(errors="replace"))
        mon_cmd(args.port, "sendkey a", 0.1)
        mon_cmd(args.port, "sendkey ret", 0.1)
        t1 = time.time()
        kbd_ok = False
        while time.time() - t1 < 12:
            delta = args.log.read_text(errors="replace")[mark2:]
            if "KERNEL PANIC" in delta:
                print("✗ panic during keyboard check", file=sys.stderr)
                return 1
            if "a" in strip_noise(delta) or "not found" in delta:
                kbd_ok = True
                break
            time.sleep(0.2)
        if not kbd_ok:
            print("✗ keyboard dead after mouse activity", file=sys.stderr)
            return 1
        print("✓ smoke-mouse-tty-isolation PASS")
        print(f"  LOG {args.log}")
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
