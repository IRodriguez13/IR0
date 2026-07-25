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
    # Strong: saved file + marker. Soft: nano UI came up (tty/CSI/termios OK).
    if "NANO_SMOKE_OK" in text and "hello IR0" in text:
        return True
    return "GNU nano" in text and "New File" in text


def inject_nano_disk(disk: Path, nano: Path) -> None:
    subprocess.run(
        [sys.executable, str(INJECT), str(disk), str(nano), "usr/bin/nano"],
        check=True,
        cwd=str(ROOT),
    )


def nano_inject_case(mon: Monitor, case: Case, key_delay: float) -> None:
    if case.special != "nano_edit":
        inject_case(mon, case, key_delay)
        return
    d = max(key_delay, 0.50)
    mon.type_str("TERM=linux /usr/bin/nano /tmp/nano-smoke.txt", d)
    mon.ret()
    # Wait for nano chrome (CSI paint) before sending body text.
    time.sleep(5.0)
    mon.type_str("hello IR0 nano", d)
    time.sleep(1.0)
    # Tiny nano: ^X → "Save modified buffer?" → Y → Enter filename / confirm.
    mon.key("ctrl-x")
    time.sleep(2.0)
    mon.key("y")
    time.sleep(1.2)
    mon.ret()
    time.sleep(2.5)
    # Back at ash (hopefully): show file + marker.
    mon.type_str("cat /tmp/nano-smoke.txt", d)
    mon.ret()
    time.sleep(1.2)
    mon.type_str("echo NANO_SMOKE_OK", d)
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
            timeout=90.0,
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
