#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
§2 baseline capture — pins, problem inventory, and optional QEMU guest suite.

Writes under out/baseline/:
  PINS.txt
  PROBLEMS.md
  CAPTURE.md   (checklist always; serial excerpt when --qemu succeeds)

Usage:
  python3 scripts/baseline_capture.py
  python3 scripts/baseline_capture.py --qemu [--timeout 180]
"""

from __future__ import annotations

import argparse
import crypt
import datetime as dt
import os
import re
import signal
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "out" / "baseline"
USERSPACE = ROOT.parent / "IR0-userspace"
PROMPT_RE = re.compile(r"[a-zA-Z0-9_-]+@[a-zA-Z0-9_-]+:\S*[#$]")
NEED_BOOT = ["RUNIT_STAGE1_OK", "GETTY_READY"]

CAPTURE_CMDS = [
    "uname -a",
    "id",
    "ps",
    "date",
    "uptime",
    "cat /proc/uptime",
    "cat /proc/loadavg",
    "cat /proc/meminfo",
    "cat /proc/iomem",
    "cat /proc/ps",
    "cat /proc/netinfo",
    "find /proc",
    "find /sys",
    "find /heart",
]


def git_pin(repo: Path) -> tuple[str, str, str]:
    if not repo.is_dir() or not (repo / ".git").exists():
        return "n/a", "n/a", "missing"
    branch = subprocess.check_output(
        ["git", "rev-parse", "--abbrev-ref", "HEAD"], cwd=repo, text=True
    ).strip()
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=repo, text=True
    ).strip()
    dirty = subprocess.check_output(
        ["git", "status", "--porcelain"], cwd=repo, text=True
    ).strip()
    return branch, commit, "dirty" if dirty else "clean"


def write_pins() -> None:
    OUT.mkdir(parents=True, exist_ok=True)
    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ")
    ir0_b, ir0_c, ir0_d = git_pin(ROOT)
    us_b, us_c, us_d = git_pin(USERSPACE)
    text = (
        f"# IR0 baseline pins\n"
        f"captured_utc={now}\n"
        f"IR0_branch={ir0_b}\n"
        f"IR0_commit={ir0_c}\n"
        f"IR0_tree={ir0_d}\n"
        f"IR0_userspace_branch={us_b}\n"
        f"IR0_userspace_commit={us_c}\n"
        f"IR0_userspace_tree={us_d}\n"
    )
    (OUT / "PINS.txt").write_text(text, encoding="utf-8")
    print(text, end="")


def write_problems() -> None:
    src = ROOT / "Documentation" / "releases" / "BASELINE_GAPS.md"
    if src.is_file():
        (OUT / "PROBLEMS.md").write_text(src.read_text(encoding="utf-8"), encoding="utf-8")
    else:
        (OUT / "PROBLEMS.md").write_text("# see Documentation/releases/BASELINE_GAPS.md\n", encoding="utf-8")
    print(f"wrote {OUT / 'PROBLEMS.md'}")


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


def mon(port: int, cmd: str, wait: float = 0.05) -> None:
    import socket

    with socket.create_connection(("127.0.0.1", port), timeout=5) as s:
        s.settimeout(1.0)
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


def type_str(port: int, s: str, delay: float = 0.05) -> None:
    for ch in s:
        if ch == " ":
            mon(port, "sendkey spc", delay)
        elif ch == "|":
            mon(port, "sendkey shift-backslash", delay)
        elif ch == "-":
            mon(port, "sendkey minus", delay)
        elif ch == "/":
            mon(port, "sendkey slash", delay)
        elif ch == ".":
            mon(port, "sendkey dot", delay)
        elif ch == "_":
            mon(port, "sendkey shift-minus", delay)
        else:
            mon(port, f"sendkey {ch}", delay)


def wait_tags(log: Path, tags: list[str], proc: subprocess.Popen[bytes], timeout: float) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "FIRSTBOOT_FAIL" in text or "KERNEL PANIC" in text:
            return False
        if all(t in text for t in tags):
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.25)
    return False


def wait_prompt(log: Path, proc: subprocess.Popen[bytes], timeout: float, after: int = 0) -> bool:
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "KERNEL PANIC" in text:
            return False
        if len(list(PROMPT_RE.finditer(text))) > after:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.3)
    return False


def wait_substr(log: Path, proc: subprocess.Popen[bytes], needle: str, timeout: float, after: int = 0) -> bool:
    """Wait until needle appears in log text beyond offset `after`."""
    deadline = time.time() + timeout
    while time.time() < deadline:
        text = read_log(log)
        if "KERNEL PANIC" in text:
            return False
        idx = text.find(needle, after)
        if idx >= 0:
            return True
        if proc.poll() is not None:
            return False
        time.sleep(0.2)
    return False


def guest_login(port: int, log: Path, proc: subprocess.Popen[bytes], user: str, password: str) -> bool:
    """Drive getty login; wait for Password: (avoid typing pwd as username)."""
    # Keyboard smokes often print on the same line as the username prompt.
    wait_tags(log, ["KBD_SCANCODE_OK"], proc, 15)
    time.sleep(0.8)

    for _attempt in range(3):
        base = len(read_log(log))
        if not wait_substr(log, proc, "Enter your Unix username:", 40, after=max(0, base - 200)):
            if PROMPT_RE.search(read_log(log)):
                return True
            return False

        type_str(port, user, delay=0.08)
        mon(port, "sendkey ret", 0.35)
        if not wait_substr(log, proc, "LOGIN_USER_READ", 20, after=base):
            continue

        # Empty-user continue re-prints the banner; wait for Password: or retry.
        deadline = time.time() + 25
        saw_pw = False
        while time.time() < deadline:
            text = read_log(log)
            chunk = text[base:]
            if "Password:" in chunk:
                saw_pw = True
                break
            if chunk.count("Enter your Unix username:") >= 2:
                break
            if "Login incorrect" in chunk:
                break
            if proc.poll() is not None:
                return False
            time.sleep(0.2)
        if not saw_pw:
            time.sleep(1.0)
            continue

        type_str(port, password, delay=0.08)
        mon(port, "sendkey ret", 0.4)
        if wait_substr(log, proc, "LOGIN_OK", 40, after=base):
            return wait_prompt(log, proc, 45)
        if wait_prompt(log, proc, 20):
            return True
        time.sleep(1.2)
    return False


def run_qemu_capture(timeout: int, port: int) -> tuple[bool, str]:
    iso = ROOT / "kernel-x64-userspace.iso"
    src = ROOT / "disk.img"
    if not iso.is_file() or not src.is_file():
        return False, "missing kernel-x64-userspace.iso or disk.img"

    user = "labuser"
    hashed = crypt.crypt("testpass", crypt.METHOD_SHA512)
    seed_body = (
        f"username={user}\n"
        f"hostname=unix\n"
        f"password_hash={hashed}\n"
        "wheel=1\n"
        "lock_root=1\n"
    )
    disk = Path(tempfile.mktemp(prefix="ir0-baseline.", suffix=".img"))
    seed_path = Path(tempfile.mktemp(prefix="ir0-baseline-seed.", suffix=".txt"))
    log_path = OUT / "qemu-serial.log"
    try:
        subprocess.run(["cp", "-f", str(src), str(disk)], check=True)
        seed_path.write_text(seed_body, encoding="utf-8")
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts" / "inject_init_minix.py"),
                str(disk),
                str(seed_path),
                "etc/firstboot.seed",
            ],
            check=True,
        )
        log_path.write_text("")
        proc = subprocess.Popen(
            [
                os.environ.get("QEMU", "qemu-system-x86_64"),
                "-cdrom",
                str(iso),
                "-drive",
                f"file={disk},format=raw,if=ide,index=0",
                "-serial",
                f"file:{log_path}",
                "-display",
                "none",
                "-m",
                "256M",
                "-no-reboot",
                "-monitor",
                f"tcp:127.0.0.1:{port},server,nowait",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if not wait_tags(log_path, NEED_BOOT + ["FIRSTBOOT_OK"], proc, min(timeout, 90)):
            if not wait_tags(log_path, NEED_BOOT, proc, 30):
                kill_qemu(proc)
                return False, "boot tags missing\n" + read_log(log_path)[-4000:]

        if not guest_login(port, log_path, proc, user, "testpass"):
            kill_qemu(proc)
            return False, "no prompt after login\n" + read_log(log_path)[-4000:]

        prompts = len(list(PROMPT_RE.finditer(read_log(log_path))))
        for cmd in CAPTURE_CMDS:
            type_str(port, cmd, delay=0.06)
            mon(port, "sendkey ret", 0.3)
            # find /proc can be large; allow longer
            wait_s = 90 if cmd.startswith("find ") else 50
            if not wait_prompt(log_path, proc, wait_s, after=prompts):
                kill_qemu(proc)
                return False, f"hang after {cmd!r}\n" + read_log(log_path)[-6000:]
            prompts = len(list(PROMPT_RE.finditer(read_log(log_path))))

        text = read_log(log_path)
        kill_qemu(proc)
        checks = [
            ("IR0 " in text and "x86_64" in text, "uname"),
            ("uid=" in text or "UID" in text, "id"),
            ("MemTotal" in text, "meminfo"),
            ("PID" in text and "CMD" in text, "proc/ps header"),
            ("/proc" in text, "find /proc"),
            ("/sys" in text, "find /sys"),
            ("/heart" in text or "heart" in text, "find /heart"),
        ]
        missing = [name for ok, name in checks if not ok]
        if missing:
            return False, "missing evidence: " + ", ".join(missing) + "\n" + text[-5000:]
        return True, text
    finally:
        seed_path.unlink(missing_ok=True)
        disk.unlink(missing_ok=True)


def write_capture(qemu_ok: bool | None, serial: str = "") -> None:
    lines = [
        "# Functional capture",
        "",
        "> **Fuente de verdad:** guest serial + CAPTURE_CMDS in scripts/baseline_capture.py",
        "",
        "Commands (guest after login):",
        "",
    ]
    for c in CAPTURE_CMDS:
        lines.append(f"- `{c}`")
    lines.append("")
    lines.append("Also: `doas su -` when doas+wheel is installed on the image.")
    lines.append("")
    if qemu_ok is True:
        lines.append("## QEMU result: PASS")
        lines.append("")
        lines.append("Serial excerpt (tail):")
        lines.append("")
        lines.append("```")
        # strip NULs for markdown
        excerpt = serial[-8000:].replace("\x00", "")
        lines.append(excerpt)
        lines.append("```")
    elif qemu_ok is False:
        lines.append("## QEMU result: FAIL")
        lines.append("")
        lines.append("```")
        lines.append(serial[-6000:].replace("\x00", ""))
        lines.append("```")
    else:
        lines.append("Run with `--qemu` to drive the guest suite (needs iso+disk).")
    (OUT / "CAPTURE.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(f"wrote {OUT / 'CAPTURE.md'}")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--qemu", action="store_true", help="Boot QEMU and run CAPTURE_CMDS")
    ap.add_argument("--timeout", type=int, default=180)
    ap.add_argument("--port", type=int, default=46388)
    args = ap.parse_args()
    write_pins()
    write_problems()
    if not args.qemu:
        write_capture(None)
        return 0
    ok, serial = run_qemu_capture(args.timeout, args.port)
    write_capture(ok, serial)
    if ok:
        print("✓ baseline QEMU capture OK")
        return 0
    print("✗ baseline QEMU capture FAILED", file=sys.stderr)
    print(serial[-2000:], file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
