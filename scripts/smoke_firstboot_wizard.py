#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Interactive firstboot wizard smoke (HMP): username + password Confirm.

Repro for #UD when sched_schedule_next ran under keyboard IRQ on Enter.
PASS: FIRSTBOOT_OK or login prompt without KERNEL PANIC / #UD.
"""

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

SPECIAL = {
    " ": "spc",
    "/": "slash",
    "-": "minus",
    ".": "dot",
    "_": "shift-minus",
}

FAIL_RE = re.compile(
    r"KERNEL PANIC|#UD\b|Unhandled kernel|KERNEL_EXECUTE_BSS|DOUBLE FAULT"
)
USER_PROMPT = re.compile(r"Enter your Unix username:")
HOST_PROMPT = re.compile(r"Hostname")
PASS_PROMPT = re.compile(r"Password:")
CONFIRM_PROMPT = re.compile(r"Confirm password:")
DONE_RE = re.compile(
    r"FIRSTBOOT_OK|Account '.+' created|login:|ASH_INTERACTIVE_READY|GETTY_READY"
)


def mon_send(port: int, cmd: str) -> None:
    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(0.4)
        try:
            s.recv(4096)
        except OSError:
            pass
        s.sendall((cmd + "\n").encode())
        time.sleep(0.05)
        try:
            s.recv(4096)
        except OSError:
            pass


def type_str(port: int, text: str, delay: float = 0.12) -> None:
    for ch in text:
        if ch in SPECIAL:
            mon_send(port, f"sendkey {SPECIAL[ch]}")
        elif ch.isupper():
            mon_send(port, f"sendkey shift-{ch.lower()}")
        else:
            mon_send(port, f"sendkey {ch}")
        time.sleep(delay)
    mon_send(port, "sendkey ret")
    time.sleep(0.35)


def wait_re(log: Path, rx: re.Pattern[str], proc: subprocess.Popen, limit: float) -> str:
    t0 = time.time()
    while time.time() - t0 < limit:
        text = log.read_text(errors="replace")
        if FAIL_RE.search(text):
            return text
        if rx.search(text):
            return text
        if proc.poll() is not None:
            return text
        time.sleep(0.2)
    return log.read_text(errors="replace")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument(
        "--disk",
        type=Path,
        default=ISD / "out/x86_64/images/desktop/disk.img",
    )
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-firstboot-wizard.log"))
    ap.add_argument("--port", type=int, default=46201)
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument("--user", default="ivan")
    ap.add_argument("--password", default="ivan")
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print(f"✗ need iso+disk: {args.iso} {args.disk}", file=sys.stderr)
        return 2

    disk = Path(tempfile.mktemp(prefix="ir0-fbwiz.", suffix=".img"))
    shutil.copy2(args.disk, disk)
    args.log.write_text("")
    subprocess.run(
        ["pkill", "-f", f"qemu-system-x86_64.*127.0.0.1:{args.port}"],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.3)

    qemu = [
        "qemu-system-x86_64",
        "-cdrom",
        str(args.iso),
        "-drive",
        f"file={disk},format=raw,if=ide,index=0",
        "-serial",
        f"file:{args.log}",
        "-display",
        "none",
        "-m",
        "512M",
        "-no-reboot",
        "-net",
        "none",
        "-monitor",
        f"tcp:127.0.0.1:{args.port},server,nowait",
    ]
    proc = subprocess.Popen(qemu, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        text = wait_re(log=args.log, rx=USER_PROMPT, proc=proc, limit=min(90.0, args.timeout))
        if FAIL_RE.search(text):
            print("✗ panic before username prompt", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1
        if not USER_PROMPT.search(text):
            print("✗ timeout waiting for username prompt", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1

        time.sleep(1.0)
        type_str(args.port, args.user)
        text = wait_re(args.log, HOST_PROMPT, proc, 40.0)
        if FAIL_RE.search(text):
            print("✗ panic after username", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1

        type_str(args.port, "unix")
        text = wait_re(args.log, PASS_PROMPT, proc, 40.0)
        if FAIL_RE.search(text):
            print("✗ panic after hostname", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1

        # Password (no echo) + Confirm — Enter used to #UD under IRQ schedule.
        type_str(args.port, args.password)
        text = wait_re(args.log, CONFIRM_PROMPT, proc, 40.0)
        if FAIL_RE.search(text):
            print("✗ panic after Password Enter (IRQ schedule bug)", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1

        type_str(args.port, args.password)
        text = wait_re(args.log, DONE_RE, proc, 60.0)
        if FAIL_RE.search(text):
            print("✗ panic after Confirm password", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1
        if not DONE_RE.search(text):
            print("✗ wizard did not complete", file=sys.stderr)
            print(text[-2500:], file=sys.stderr)
            return 1

        print("✓ smoke-firstboot-wizard OK")
        return 0
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except Exception:
                proc.kill()
        disk.unlink(missing_ok=True)


if __name__ == "__main__":
    sys.exit(main())
