#!/usr/bin/env python3
"""
Run a minimal runtime smoke check for the network stack under QEMU.
"""

import argparse
import os
import re
import subprocess
import sys


KERNEL_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CONFIG_PATH = os.path.join(KERNEL_ROOT, ".config")
ISO_PATH = os.path.join(KERNEL_ROOT, "kernel-x64.iso")
DISK_PATH = os.path.join(KERNEL_ROOT, "disk.img")


def config_enabled(symbol: str) -> bool:
    if not os.path.exists(CONFIG_PATH):
        return True
    key = f"CONFIG_{symbol}=y"
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        return any(line.strip() == key for line in f)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="IR0 runtime network smoke check")
    parser.add_argument("--timeout-sec", type=int, default=25, help="QEMU timeout in seconds")
    return parser.parse_args()


def main() -> int:
    args = parse_args()

    if not config_enabled("ENABLE_NETWORKING"):
        print("[RUNTIME-NET] CONFIG_ENABLE_NETWORKING=n, skipping runtime check")
        return 0

    if not os.path.exists(ISO_PATH):
        print("[RUNTIME-NET] kernel-x64.iso not found", file=sys.stderr)
        return 2
    if not os.path.exists(DISK_PATH):
        print("[RUNTIME-NET] disk.img not found", file=sys.stderr)
        return 2

    cmd = [
        "qemu-system-x86_64",
        "-cdrom",
        ISO_PATH,
        "-drive",
        f"file={DISK_PATH},format=raw,if=ide,index=0",
        "-netdev",
        "user,id=net0",
        "-device",
        "rtl8139,netdev=net0",
        "-m",
        "256M",
        "-no-reboot",
        "-no-shutdown",
        "-display",
        "none",
        "-serial",
        "stdio",
    ]

    print("[RUNTIME-NET] launching QEMU runtime smoke")
    try:
        proc = subprocess.run(
            cmd,
            cwd=KERNEL_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=args.timeout_sec,
        )
        output = proc.stdout or ""
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        print("[RUNTIME-NET] QEMU timeout reached, evaluating captured output")

    required_patterns = [
        r"Initializing network stack",
        r"Registered protocol: ARP",
        r"Registered protocol: IP",
        r"Registered protocol: ICMP",
        r"Registered protocol: UDP",
    ]

    missing = [pat for pat in required_patterns if not re.search(pat, output)]
    if missing:
        print("[RUNTIME-NET] missing expected network markers:")
        for pat in missing:
            print(f"  - {pat}")
        return 1

    if re.search(r"panic|kernel panic", output, flags=re.IGNORECASE):
        print("[RUNTIME-NET] panic detected during runtime smoke")
        return 1

    print("[RUNTIME-NET] runtime smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
