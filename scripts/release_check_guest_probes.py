#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Release gate guest probes (Tier 1.5b).

After runit boot + seeded firstboot, log in and exercise a minimal shell
session. PASS when serial shows expected uname, PID1, and BusyBox markers.
"""

from __future__ import annotations

import argparse
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
sys.path.insert(0, str(ROOT / "scripts"))

DEFAULT_TIMEOUT = 120
MONITOR_PORT = 4450
SENDKEY_DELAY_SEC = 0.10

RUNIT_TAGS = [
    "RUNIT_STAGE1_OK",
    "RUNIT_STAGE2_OK",
    "GETTY_READY",
]

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE PANIC"),
    re.compile(r"General protection fault|GPF_IN_USERSPACE"),
    re.compile(r"RUNSV_CONSOLE_EXEC_FAIL"),
    re.compile(r"FIRSTBOOT_FAIL"),
]

PASS_TAG = "RELEASE_GUEST_PROBE_OK"


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


def send_keys(port: int, keys: list[str], delay: float = SENDKEY_DELAY_SEC) -> None:
    for key in keys:
        monitor_send(port, f"sendkey {key}")
        time.sleep(delay)


def text_to_keys(text: str) -> list[str]:
    mapping = {
        " ": "spc",
        "\t": "tab",
        "\n": "ret",
        "-": "minus",
        "/": "slash",
        ".": "dot",
    }
    keys: list[str] = []
    for ch in text:
        if ch in mapping:
            keys.append(mapping[ch])
        elif ch.isalnum():
            keys.append(ch.lower())
        else:
            raise ValueError(f"unsupported character for sendkey: {ch!r}")
    keys.append("ret")
    return keys


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return path.read_text(errors="replace")


def kill_qemu(proc: subprocess.Popen[bytes]) -> None:
    if proc.poll() is not None:
        return
    try:
        proc.send_signal(signal.SIGTERM)
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=5)
    except ProcessLookupError:
        pass


def cleanup_stale_qemu(monitor_port: int) -> None:
    subprocess.run(
        [
            "pkill",
            "-f",
            f"qemu-system-x86_64.*127.0.0.1:{monitor_port}",
        ],
        check=False,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    time.sleep(0.5)


def resolve_version_string() -> str:
    result = subprocess.run(
        [
            "make",
            "-s",
            "-f",
            str(ROOT / "Makefile"),
            "-pn",
        ],
        text=True,
        capture_output=True,
        check=False,
        cwd=ROOT,
    )
    for line in result.stdout.splitlines():
        if line.startswith("IR0_VERSION_STRING := "):
            return line.split(":= ", 1)[1].strip()
    return ""


def inject_firstboot_seed(disk: Path) -> None:
    import crypt

    seed_path = Path(tempfile.mktemp(prefix="ir0-release-seed.", suffix=".txt"))
    try:
        hashed = crypt.crypt("testpass", crypt.METHOD_SHA512)
        seed_path.write_text(
            "username=labuser\n"
            "hostname=unix\n"
            f"password_hash={hashed}\n"
            "wheel=1\n"
            "lock_root=1\n"
            "recovery=1\n",
            encoding="utf-8",
        )
        inject = ROOT / "scripts" / "inject_init_minix.py"
        subprocess.run(
            [sys.executable, str(inject), str(disk), str(seed_path), "etc/firstboot.seed"],
            check=True,
            cwd=ROOT,
        )
    finally:
        seed_path.unlink(missing_ok=True)


def login_prompt_ready(text: str) -> bool:
    if not all(tag in text for tag in RUNIT_TAGS):
        return False
    return "FIRSTBOOT_OK" in text or "login:" in text.lower()


def login_keys() -> tuple[list[str], list[str]]:
    user = list("labuser") + ["ret"]
    passwd = list("testpass") + ["ret"]
    return user, passwd


def probes_satisfied(text: str, version: str) -> tuple[bool, list[str]]:
    missing: list[str] = []
    version_pat = re.escape(version) if version else r"0\.0\.1"
    if not re.search(rf"IR0.*{version_pat}|{version_pat}.*IR0", text, re.I):
        if version and version not in text:
            missing.append(f"uname/version ({version})")
    if "runit" not in text.lower() and "/sbin/runit" not in text:
        if not re.search(r"(?:^|\n)runit(?:\n|$)", text):
            missing.append("PID1 runit (/proc/1/comm)")
    if "BusyBox v" not in text and "busybox" not in text.lower():
        missing.append("BusyBox userspace")
    return len(missing) == 0, missing


def run_probes(args: argparse.Namespace) -> int:
    cleanup_stale_qemu(args.monitor_port)

    iso = Path(args.iso)
    src_disk = Path(args.disk)
    if not iso.is_file():
        print(f"✗ missing ISO: {iso}", file=sys.stderr)
        return 1
    if not src_disk.is_file():
        print(f"✗ missing disk: {src_disk}", file=sys.stderr)
        return 1

    version = args.version or resolve_version_string()
    if not version:
        print("✗ could not resolve IR0_VERSION_STRING", file=sys.stderr)
        return 1

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)

    disk = Path(tempfile.mktemp(prefix="ir0-release-guest.", suffix=".img"))
    shutil.copy2(src_disk, disk)
    inject_firstboot_seed(disk)

    monitor = f"tcp:127.0.0.1:{args.monitor_port},server,nowait"
    qemu_cmd = [
        args.qemu,
        "-cdrom",
        str(iso),
        "-drive",
        f"file={disk},format=raw,if=ide,index=0",
        "-serial",
        f"file:{log_path}",
        "-display",
        "none",
        "-monitor",
        monitor,
        "-m",
        "256M",
        "-no-reboot",
        "-net",
        "none",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    deadline = time.monotonic() + args.timeout
    login_sent = False
    probes_sent = False

    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None and proc.returncode not in (0, None):
                print(f"✗ QEMU exited early ({proc.returncode})", file=sys.stderr)
                break

            text = read_log(log_path)
            for pat in FAIL_RES:
                if pat.search(text):
                    print(f"✗ guest probe fail pattern: {pat.pattern}", file=sys.stderr)
                    return 1

            if not login_sent and login_prompt_ready(text):
                user_keys, pass_keys = login_keys()
                time.sleep(0.8)
                send_keys(args.monitor_port, user_keys)
                time.sleep(0.5)
                send_keys(args.monitor_port, pass_keys)
                login_sent = True
                time.sleep(1.5)

            if login_sent and not probes_sent and "#" in text:
                send_keys(args.monitor_port, text_to_keys("uname -a"))
                time.sleep(1.0)
                send_keys(args.monitor_port, text_to_keys("cat /proc/1/comm"))
                time.sleep(0.8)
                send_keys(args.monitor_port, text_to_keys("busybox"))
                time.sleep(1.0)
                probes_sent = True

            if probes_sent:
                text = read_log(log_path)
                ok, missing = probes_satisfied(text, version)
                if ok:
                    print(f"✓ {PASS_TAG} version={version}")
                    return 0
                if time.monotonic() > deadline - 5:
                    print(f"✗ guest probes missing: {', '.join(missing)}", file=sys.stderr)
                    return 1

            time.sleep(0.35)

        text = read_log(log_path)
        ok, missing = probes_satisfied(text, version)
        if ok:
            print(f"✓ {PASS_TAG} version={version}")
            return 0
        print(f"✗ timeout; missing: {', '.join(missing)}", file=sys.stderr)
        return 1
    finally:
        kill_qemu(proc)
        disk.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Release guest probes smoke")
    parser.add_argument("--iso", default="kernel-x64-userspace.iso")
    parser.add_argument("--disk", default="disk.img")
    parser.add_argument("--log", default="/tmp/release-guest-probes.log")
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    parser.add_argument("--version", default="")
    return run_probes(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
