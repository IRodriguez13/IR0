#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Runit PID1 + BusyBox ash interactive smoke (headless + QEMU monitor sendkey).

Gate contract (D1.16b): after runit boot + ash ready, inject ``echo hi`` once,
then PASS as soon as SYS_READ_RETURN_OK + ASH_COMMAND_ECHO_OK appear in serial.
``ASH_COMMAND_ECHO_OK`` is emitted only after the kernel observes ``hi\\n`` on
ash stdout (``includes/ir0/ash_smoke.c``); do not require a separate literal
``hi`` line in the serial log — console/logger interleaving can omit it.
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
DEFAULT_TIMEOUT = 90
MONITOR_PORT = 4446
ECHO_STABILIZE_SEC = 2.0
ECHO_RETRY_SEC = 12.0
ECHO_RETRY_FAST_SEC = 2.5
ECHO_RETRY_MIN_SEC = 1.0
ECHO_RETRY_MAX = 6
SENDKEY_DELAY_SEC = 0.12
PROMPT_AUDIT_DEFER_SEC = 6.0
SMOKE_ATTEMPTS = 3

RUNIT_TAGS = [
    "RUNIT_STAGE1_OK",
    "RUNIT_STAGE2_OK",
    "RUNSV_CONSOLE_START",
    "RUNSV_LOGGER_START",
]

PASS_TAGS = [
    "SYS_READ_RETURN_OK",
    "ASH_COMMAND_ECHO_OK",
]

PREREQ_TAGS = [
    "ASH_INTERACTIVE_READY",
]

DIAG_TAGS = [
    "KBD_USER_POLL_OK",
    "TTY_CANON_LINE_READY",
]

FAIL_RES = [
    re.compile(r"KERNEL PANIC"),
    re.compile(r"DOUBLE PANIC"),
    re.compile(r"General protection fault|GPF_IN_USERSPACE"),
    re.compile(r"UD_FAULT_RIP="),
    re.compile(r"\[FASE[0-9A-Z]+\]\[FAIL\]"),
    re.compile(r"RUNSV_CONSOLE_EXEC_FAIL"),
]

BUSYBOX_CONFIG_PROMPT = re.compile(
    r"PASSWORD_MINLEN|FEATURE_SHADOWPASSWDS|ASH_IDLE_TIMEOUT|\(NEW\)"
)
ASH_PROMPT_RE = re.compile(r"(?:^|\n)#\s")
ASH_PROMPT_STABLE_RE = re.compile(r"(?:^|\n)#\s*KBD_")
ASH_PROMPT_AUDIT_RE = re.compile(r"(?:^|\n)#\s*\[WAIT_EXIT")
ECHO_GARBLED_RE = re.compile(
    r"\bechoo\b|\bcho hi\b|: not found|: Invalid argument"
)

ECHO_KEYS = ["e", "c", "h", "o", "spc", "h", "i", "ret"]

SERIAL_SMOKE_TAGS = RUNIT_TAGS + PREREQ_TAGS + PASS_TAGS + DIAG_TAGS


def ash_ready_for_input(text: str, busybox_seen_at: float, now: float) -> bool:
    runit_ok, _ = log_has_tags(text, RUNIT_TAGS)
    if not runit_ok:
        return False
    if "ASH_INTERACTIVE_READY" not in text:
        return False
    if "BusyBox v" not in text:
        return False
    if not ASH_PROMPT_RE.search(text):
        return False
    if ASH_PROMPT_STABLE_RE.search(text):
        return True
    if ASH_PROMPT_AUDIT_RE.search(text):
        if busybox_seen_at <= 0.0 or now - busybox_seen_at < PROMPT_AUDIT_DEFER_SEC:
            return False
    return True


def echo_command_failed(text: str) -> bool:
    if "ASH_COMMAND_ECHO_OK" in text:
        return False
    if ECHO_GARBLED_RE.search(text):
        return True
    if "SYS_READ_RETURN_OK" in text and not log_has_hi(text):
        return True
    return False


def normalize_serial_smoke(text: str) -> str:
    """Rejoin smoke tags split across serial line boundaries."""
    out = text
    for tag in SERIAL_SMOKE_TAGS:
        if tag in out:
            continue
        for i in range(1, len(tag)):
            for sep in ("\n", "\r\n"):
                frag = tag[:i] + sep + tag[i:]
                if frag in out:
                    out = out.replace(frag, tag)
    return out


def log_has_tags(text: str, tags: list[str]) -> tuple[bool, list[str]]:
    missing = [t for t in tags if t not in text]
    return len(missing) == 0, missing


def log_has_hi(text: str) -> bool:
    return "hi\n" in text or "hi\r\n" in text


def pass_satisfied(text: str) -> bool:
    text = normalize_serial_smoke(text)
    ok, _ = log_has_tags(text, PASS_TAGS)
    return ok


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


def clear_prompt_line(port: int) -> None:
    send_keys(port, ["ctrl-u"], delay=0.08)
    time.sleep(0.35)
    monitor_send(port, "sendkey ret")
    time.sleep(0.65)


def read_log(path: Path) -> str:
    if not path.is_file():
        return ""
    return normalize_serial_smoke(path.read_text(errors="replace"))


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
    """Drop orphaned smokes still bound to our monitor port."""
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


def classify_failure(
    text: str,
    echo_sent: bool,
    post_success: bool,
) -> str:
    if BUSYBOX_CONFIG_PROMPT.search(text):
        return "A) config prompt in log"
    if not echo_sent:
        if "ASH_INTERACTIVE_READY" not in text:
            return "B) missing ASH_INTERACTIVE_READY / ash prompt"
        if "SYS_READ_RETURN_OK" not in text:
            return "B) missing SYS_READ_RETURN_OK"
        return "B) echo sendkey did not run or read stalled"
    if "SYS_READ_RETURN_OK" not in text:
        return "B) missing SYS_READ_RETURN_OK"
    if "ASH_COMMAND_ECHO_OK" not in text:
        if ECHO_GARBLED_RE.search(text):
            return "C) garbled sendkey (echoo / not found) — retry or timing"
        return "C) missing ASH_COMMAND_ECHO_OK"
    if post_success:
        return "D) timeout after success tags (harness)"
    if not log_has_hi(text):
        return "E) missing literal hi in serial (diagnostic only)"
    return "F) timeout before success / QEMU hang"


def run_once(args: argparse.Namespace) -> int:
    cleanup_stale_qemu(args.monitor_port)

    log_path = Path(args.log)
    log_path.unlink(missing_ok=True)

    iso = Path(args.iso)
    src_disk = Path(args.disk)
    if not iso.is_file():
        print(f"✗ missing ISO: {iso}", file=sys.stderr)
        return 1
    if not src_disk.is_file():
        print(f"✗ missing disk: {src_disk} — run make load-userspace-runit", file=sys.stderr)
        return 1

    disk = Path(tempfile.mktemp(prefix="ir0-runit-ash-smoke.", suffix=".img"))
    shutil.copy2(src_disk, disk)
    monitor = f"tcp:127.0.0.1:{args.monitor_port},server,nowait"

    qemu_cmd = [
        args.qemu,
        "-cdrom", str(iso),
        "-drive", f"file={disk},format=raw,if=ide,index=0",
        "-serial", f"file:{log_path}",
        "-display", "none",
        "-monitor", monitor,
        "-m", "256M",
        "-no-reboot",
        "-net", "none",
    ]

    proc = subprocess.Popen(
        qemu_cmd,
        cwd=ROOT,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        start_new_session=True,
    )

    deadline = time.monotonic() + args.timeout
    echo_sent = False
    echo_sent_at = 0.0
    echo_retries = 0
    busybox_seen_at = 0.0
    start = time.monotonic()

    def inject_echo(*, retry: bool = False) -> bool:
        try:
            if retry:
                clear_prompt_line(args.monitor_port)
            send_keys(args.monitor_port, ECHO_KEYS)
            return True
        except OSError as exc:
            print(f"✗ monitor key injection failed: {exc}")
            print("  classify: A) QEMU monitor sendkey failed")
            return False

    try:
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                break

            text = read_log(log_path)
            now = time.monotonic()

            if "BusyBox v" in text and busybox_seen_at <= 0.0:
                busybox_seen_at = now

            for pat in FAIL_RES:
                if pat.search(text):
                    print(f"✗ smoke-runit-ash-interactive FAIL pattern: {pat.pattern}")
                    print("  classify: G) kernel panic / fault in log")
                    return 1

            if pass_satisfied(text):
                elapsed = time.monotonic() - start
                kill_qemu(proc)
                print(
                    f"✓ smoke-runit-ash-interactive PASS "
                    f"(SYS_READ_RETURN_OK + ASH_COMMAND_ECHO_OK, {elapsed:.1f}s)"
                )
                return 0

            if ash_ready_for_input(text, busybox_seen_at, now) and not echo_sent:
                time.sleep(ECHO_STABILIZE_SEC)
                if inject_echo(retry=False):
                    echo_sent = True
                    echo_sent_at = time.monotonic()
                else:
                    return 1

            if echo_sent and echo_retries < ECHO_RETRY_MAX and echo_sent_at > 0.0:
                elapsed = time.monotonic() - echo_sent_at
                if elapsed < ECHO_RETRY_MIN_SEC:
                    time.sleep(0.15)
                    continue
                need_retry = False
                if echo_command_failed(text):
                    need_retry = elapsed >= ECHO_RETRY_FAST_SEC
                elif "ASH_COMMAND_ECHO_OK" not in text:
                    threshold = ECHO_RETRY_FAST_SEC if "SYS_READ_RETURN_OK" in text else ECHO_RETRY_SEC
                    need_retry = elapsed >= threshold
                if need_retry:
                    if inject_echo(retry=True):
                        echo_retries += 1
                        echo_sent_at = time.monotonic()
                    else:
                        return 1

            time.sleep(0.15)

        text = read_log(log_path)
        if pass_satisfied(text):
            elapsed = time.monotonic() - start
            kill_qemu(proc)
            print(
                f"✓ smoke-runit-ash-interactive PASS "
                f"(SYS_READ_RETURN_OK + ASH_COMMAND_ECHO_OK, {elapsed:.1f}s, final read)"
            )
            return 0

        bucket = classify_failure(text, echo_sent, False)
        print(f"✗ smoke-runit-ash-interactive timeout ({args.timeout}s)")
        print(f"  classify: {bucket}")

        runit_ok, runit_missing = log_has_tags(text, RUNIT_TAGS)
        if not runit_ok:
            for tag in runit_missing:
                print(f"  - missing {tag}")
        for tag in PREREQ_TAGS + PASS_TAGS:
            if tag not in text:
                print(f"  - missing {tag}")
        if echo_sent:
            print("  - echo key injection ran")
        else:
            print("  - echo key injection did not run")
        if not log_has_hi(text):
            print("  - literal output 'hi'")
        for tag in DIAG_TAGS:
            if tag not in text:
                print(f"  - diag missing {tag}")

        classify = ROOT / "scripts" / "ktm_log_classify.py"
        if classify.is_file():
            try:
                out = subprocess.run(
                    [sys.executable, str(classify), str(log_path)],
                    capture_output=True,
                    text=True,
                    check=False,
                )
                if out.stdout.strip():
                    print(out.stdout.rstrip())
            except OSError as exc:
                print(f"  - ktm_log_classify: {exc}")
        return 1
    finally:
        kill_qemu(proc)
        disk.unlink(missing_ok=True)


def main() -> int:
    parser = argparse.ArgumentParser(description="Runit + ash interactive smoke")
    parser.add_argument("--log", default="/tmp/runit-ash-smoke.log")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT)
    parser.add_argument("--qemu", default=os.environ.get("QEMU", "qemu-system-x86_64"))
    parser.add_argument("--iso", default=str(ROOT / "kernel-x64-userspace.iso"))
    parser.add_argument("--disk", default=str(ROOT / "disk.img"))
    parser.add_argument("--monitor-port", type=int, default=MONITOR_PORT)
    args = parser.parse_args()

    for attempt in range(1, SMOKE_ATTEMPTS + 1):
        if attempt > 1:
            print("  RETRY   smoke-runit-ash-interactive (second QEMU boot)")
        rc = run_once(args)
        if rc == 0:
            return 0
    return rc


if __name__ == "__main__":
    sys.exit(main())
