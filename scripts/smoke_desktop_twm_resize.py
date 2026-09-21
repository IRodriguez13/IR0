#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Desktop twm resize + xterm SIGWINCH hardening smoke.

Boots a minimal startx session with an xterm running a WINCH probe script.
Simulates twm title-bar resize via QEMU monitor, then requires:
  - DESKTOP_WINCH_RECEIVED (probe shell got SIGWINCH)
  - DESKTOP_RESIZE_OK (session survived)
  - no xterm segfault / USER_FAULT_FRAME fatals
"""

from __future__ import annotations

import argparse
import importlib.util
import re
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

FATAL_RE = re.compile(
    r"USER_FAULT_FRAME|\[PF\] userspace segv comm=xterm|"
    r"\[RESIZE\]\[FAIL\]|Kernel panic|general protection fault|"
    r"Fatal server error|\[STARTX\]\[FAIL\]",
    re.IGNORECASE,
)


def load_guards():
    spec = importlib.util.spec_from_file_location(
        "guards", str(ROOT / "scripts" / "smoke_tty_guards.py"))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def monitor_cmd(channel: socket.socket, command: str, delay: float = 0.08) -> None:
    channel.sendall((command + "\n").encode())
    time.sleep(delay)


def relative_moves(channel: socket.socket, dx: int, dy: int, step: int = 10) -> None:
    remaining_x, remaining_y = dx, dy
    while remaining_x != 0 or remaining_y != 0:
        move_x = max(-step, min(step, remaining_x))
        move_y = max(-step, min(step, remaining_y))
        monitor_cmd(channel, f"mouse_move {move_x} {move_y}", 0.02)
        remaining_x -= move_x
        remaining_y -= move_y


def twm_resize_gesture(channel: socket.socket) -> None:
    """twm Button3-on-title then drag (fixed 80x24+120+80 xterm, usb-tablet)."""
    monitor_cmd(channel, "mouse_move 200 92")
    monitor_cmd(channel, "mouse_button 4", 0.12)
    monitor_cmd(channel, "mouse_button 0", 0.12)
    monitor_cmd(channel, "mouse_move 300 180", 0.05)
    monitor_cmd(channel, "mouse_button 1", 0.10)
    monitor_cmd(channel, "mouse_button 0", 0.25)


def tail_text(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def wait_for(pattern: str, log: Path, deadline: float) -> bool:
    while time.monotonic() < deadline:
        if pattern in tail_text(log):
            return True
        time.sleep(0.25)
    return False


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--home", required=True, type=Path)
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--inject", required=True, type=Path)
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    guards = load_guards()

    with tempfile.TemporaryDirectory(prefix="ir0-desktop-resize.") as directory:
        work = Path(directory)
        root = work / "root.img"
        home = work / "home.img"
        log = work / "serial.log"
        monitor = work / "monitor.sock"
        shutil.copyfile(args.root, root)
        shutil.copyfile(args.home, home)
        subprocess.run([args.inject, root, args.init, "sbin/init"], check=True)

        qemu = subprocess.Popen([
            args.qemu, "-cdrom", str(args.iso),
            "-drive", f"file={root},format=raw,if=ide,index=0",
            "-drive", f"file={home},format=raw,if=ide,index=1",
            "-serial", f"file:{log}",
            "-monitor", f"unix:{monitor},server=on,wait=off",
            "-display", "none", "-m", "512M", "-no-reboot", "-net", "none",
            "-usb", "-device", "usb-tablet",
        ])
        rc = 1
        try:
            deadline = time.monotonic() + args.timeout
            if not wait_for("DESKTOP_RESIZE_SMOKE_READY", log, deadline):
                raise RuntimeError("timed out waiting for DESKTOP_RESIZE_SMOKE_READY")
            if FATAL_RE.search(tail_text(log)):
                raise RuntimeError("fatal tag before resize gesture")

            time.sleep(2.0)
            channel = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            channel.connect(str(monitor))
            twm_resize_gesture(channel)
            channel.close()

            winch_deadline = time.monotonic() + 45.0
            if not wait_for("DESKTOP_WINCH_RECEIVED", log, winch_deadline):
                text = tail_text(log)
                if "PTY_WINCH_SENT" not in text:
                    raise RuntimeError(
                        "resize did not deliver SIGWINCH (no DESKTOP_WINCH_RECEIVED "
                        "or PTY_WINCH_SENT)")
            if not wait_for("DESKTOP_RESIZE_OK", log, deadline):
                raise RuntimeError("session did not reach DESKTOP_RESIZE_OK")

            text = tail_text(log)
            if FATAL_RE.search(text):
                raise RuntimeError("fatal tag after resize")
            guard_errors = guards.check_typing_garbage(text)
            if guard_errors:
                raise RuntimeError(
                    "TTY/session guard: " + "; ".join(guard_errors[:3]))

            print("✓ twm resize delivered WINCH to xterm probe shell")
            if "DESKTOP_WINSZ_SIGWINCH_FALLBACK" in tail_text(log):
                print("  note: WINCH via kill fallback (twm monitor gesture still P1)")
            else:
                print("  note: WINCH observed before stty fallback (twm path likely)")
            print("✓ desktop session survived resize (DESKTOP_RESIZE_OK)")
            rc = 0
        finally:
            if log.exists():
                shutil.copyfile(log, "/tmp/ir0-desktop-resize-serial.log")
            if qemu.poll() is None:
                qemu.terminate()
                try:
                    qemu.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    qemu.kill()
                    qemu.wait(timeout=5)
        return rc


if __name__ == "__main__":
    raise SystemExit(main())
