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
    parser.add_argument("--min-dhcp-events", type=int, default=0,
                        help="Minimum DHCP progress markers required")
    parser.add_argument("--max-tx-recoveries", type=int, default=6,
                        help="Maximum accepted RTL8139 TX recovery warnings")
    parser.add_argument("--require-dns-init", action="store_true",
                        help="Fail when DNS init marker is missing")
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

    dhcp_markers = [
        r"DHCP handshake completed successfully",
        r"DHCP unavailable, keeping static IPv4 configuration",
        r"No network device available, skipping DHCP",
    ]
    dhcp_events = sum(len(re.findall(pat, output)) for pat in dhcp_markers)
    if dhcp_events < args.min_dhcp_events:
        print(f"[RUNTIME-NET] DHCP markers too low: {dhcp_events} < {args.min_dhcp_events}")
        return 1

    if args.require_dns_init and not re.search(r"Initialized DNS client", output):
        print("[RUNTIME-NET] missing DNS initialization marker")
        return 1

    tx_recoveries = len(re.findall(r"TX path recovered", output))
    if tx_recoveries > args.max_tx_recoveries:
        print(f"[RUNTIME-NET] excessive TX recoveries: {tx_recoveries} > {args.max_tx_recoveries}")
        return 1

    hard_fail_patterns = [
        r"net_send: dev->send returned -1",
        r"Failed to send IP packet",
        r"RX read_offset out of bounds",
        r"panic|kernel panic",
    ]
    hard_fails = [pat for pat in hard_fail_patterns if re.search(pat, output, flags=re.IGNORECASE)]
    if hard_fails:
        print("[RUNTIME-NET] failure markers detected:")
        for pat in hard_fails:
            print(f"  - {pat}")
        return 1

    icmp_sent = len(re.findall(r"Sending Echo Request", output))
    icmp_replied = len(re.findall(r"Echo Reply received", output))
    if icmp_sent > 0 and icmp_replied == 0:
        print("[RUNTIME-NET] ICMP traffic seen without replies")
        return 1

    print("[RUNTIME-NET] runtime smoke passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
