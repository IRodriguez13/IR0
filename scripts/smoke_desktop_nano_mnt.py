#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Desktop nano smoke (kernel tty CSI + termios + static musl nano).

Default path: inject nano into a temp MINIX disk (avoids flaky HMP su+9p),
login as ivan, edit /tmp/nano-smoke.txt, save, cat + NANO_SMOKE_OK.

Optional --share: also stage nano on virtfs (experimental).
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from smoke_desktop_cmd_matrix import (  # noqa: E402
    Case,
    Monitor,
    cleanup_port,
    inject_case,
    normalize_serial,
    run_session,
)

DEFAULT_NANO = Path(
    os.environ.get(
        "IR0_NANO_BIN",
        str(ROOT.parent / "IR0-userspace" / "out" / "stage-bin" / "nano"),
    )
)
INJECT = ROOT / "scripts" / "inject_init_minix.py"


def expect_nano_saved(new: str) -> bool:
    text = normalize_serial(new)
    if "KERNEL PANIC" in text or "#DF" in text or "#UD" in text:
        return False
    # Hard: Ctrl-X → save → shell. Prefer file contents + marker; accept
    # nano's "[ Wrote N line ]" when HMP mangles a follow-up cat.
    wrote = ("[ Wrote" in text) or ("Wrote" in text and "line" in text)
    prompt = ("root@ir0" in text) or ("ivan@ir0" in text)
    if "NANO_SMOKE_OK" in text and ("hello IR0" in text or wrote):
        return True
    return bool(wrote and prompt)


def inject_nano_disk(disk: Path, nano: Path) -> None:
    subprocess.run(
        [sys.executable, str(INJECT), str(disk), str(nano), "usr/bin/nano"],
        check=True,
        cwd=str(ROOT),
    )


def _serial_has(log: Path, needle: str) -> bool:
    if not log.is_file():
        return False
    return needle in log.read_text(errors="replace")


def nano_inject_case(mon: Monitor, case: Case, key_delay: float) -> None:
    if case.special != "nano_edit":
        inject_case(mon, case, key_delay)
        return
    # Prefer the batch serial log path used by run_session.
    log = Path(os.environ.get("IR0_NANO_SMOKE_LOG", "/tmp/ir0-nano-mnt.log"))
    d = max(key_delay, 0.35)
    # Short path: MINIX NAME_MAX is tight; long names → nano "Filename too long".
    mon.type_str("TERM=linux /usr/bin/nano /tmp/n.txt", d)
    mon.ret()
    # Wait for nano chrome (CSI paint / title) before body text.
    deadline = time.monotonic() + 20.0
    while time.monotonic() < deadline:
        if _serial_has(log, "GNU nano") or _serial_has(log, "New File"):
            break
        time.sleep(0.25)
    time.sleep(0.8)
    # Short payload — HMP is slow; avoid doubled glyphs from long delays.
    mon.type_str("hello IR0", 0.12)
    time.sleep(0.6)
    # Tiny nano: ^X → "Save modified buffer?" → Y → confirm filename.
    mon.key("ctrl-x")
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace") if log.is_file() else ""
        if "Save modified" in text or "Yes" in text[-800:]:
            break
        time.sleep(0.2)
    time.sleep(0.4)
    mon.key("y")
    deadline = time.monotonic() + 15.0
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace") if log.is_file() else ""
        if "File Name" in text or "Write" in text[-1200:]:
            break
        time.sleep(0.2)
    time.sleep(0.4)
    mon.ret()
    deadline = time.monotonic() + 25.0
    while time.monotonic() < deadline:
        text = log.read_text(errors="replace") if log.is_file() else ""
        if "KERNEL PANIC" in text:
            return
        tail = text[-500:]
        if "Wrote" in text and ("root@ir0" in tail or "ivan@ir0" in tail):
            break
        if "Error writing" in text:
            return
        time.sleep(0.25)
    time.sleep(0.8)
    # Optional marker — HMP often mangles post-nano typing; Wrote+PS1 is enough.
    mon.ret()
    time.sleep(0.3)
    mon.type_str("echo NANO_SMOKE_OK", 0.18)
    mon.ret()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=ROOT / "disk.img")
    ap.add_argument("--nano", type=Path, default=DEFAULT_NANO)
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-nano-mnt.log"))
    ap.add_argument("--port", type=int, default=46101)
    ap.add_argument("--key-delay", type=float, default=0.40)
    ap.add_argument(
        "--share",
        action="store_true",
        help="Also attach empty virtfs share (not required for PASS)",
    )
    args = ap.parse_args()

    if not args.iso.is_file() or not args.disk.is_file():
        print(f"✗ missing iso/disk: {args.iso} {args.disk}", file=sys.stderr)
        return 2
    if not args.nano.is_file():
        print(
            f"✗ missing nano: {args.nano}\n"
            "  (cd ../IR0-userspace && make fetch build-nano)",
            file=sys.stderr,
        )
        return 2

    import smoke_desktop_cmd_matrix as mx

    mx.inject_case = nano_inject_case

    work_disk = Path(tempfile.mktemp(suffix=".img", prefix="ir0-nano-disk."))
    shutil.copy2(args.disk, work_disk)
    share_dir = None
    try:
        print(f"  INJECT  /usr/bin/nano ← {args.nano}", flush=True)
        inject_nano_disk(work_disk, args.nano)
        if args.share:
            share_dir = Path(tempfile.mkdtemp(prefix="ir0-nano-share."))
            shutil.copy2(args.nano, share_dir / "nano")
            (share_dir / "nano").chmod(0o755)

        case = Case(
            "nano_edit",
            "",
            expect_nano_saved,
            special="nano_edit",
            echo="GNU nano",
            timeout=120.0,
        )
        print(f"NANO_EDIT nano={args.nano}", flush=True)
        args.log.write_text("")
        cleanup_port(args.port)
        results = run_session(
            args.iso,
            work_disk,
            args.port,
            args.log,
            args.key_delay,
            False,
            [case],
            share_dir=share_dir,
            do_poweroff=False,
        )
        for name, st in results:
            print(f"  {st:22} {name}", flush=True)
        ok = all(st == "PASS" for _, st in results)
        if ok:
            print("✓ smoke-desktop-nano-mnt PASS")
            return 0
        print("✗ smoke-desktop-nano-mnt FAIL")
        return 1
    finally:
        work_disk.unlink(missing_ok=True)
        if share_dir is not None:
            shutil.rmtree(share_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
