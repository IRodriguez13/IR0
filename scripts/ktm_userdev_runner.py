#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host runner for KTM /dev/ktm userspace pilots (optional virtio-9p host share)."""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent


def main() -> int:
    ap = argparse.ArgumentParser(description="Run IR0 KTM userdev pilot via QEMU")
    ap.add_argument("--log", default="/tmp/ktm-userdev-run.log")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--init", default=str(ROOT / "tests/ktm/userdev/ktm_fork_wait_case"))
    ap.add_argument("--done", default="KTM_USERDEV_OK")
    ap.add_argument(
        "--require",
        action="append",
        default=[],
        help="Substring that must appear in the log (repeatable)",
    )
    ap.add_argument(
        "--share",
        default="",
        help="Host directory for QEMU virtio-9p (mount_tag=ir0share). Empty = no virtfs.",
    )
    ap.add_argument(
        "--host-file",
        default="",
        help="Relative path under --share that must exist after the smoke (e.g. ktm_fork_storm.txt)",
    )
    ap.add_argument(
        "--host-grep",
        default="",
        help="Substring that must appear in --host-file after the smoke",
    )
    ap.add_argument(
        "--inject",
        action="append",
        default=[],
        metavar="SRC:DEST",
        help="Inject host file SRC into disk as DEST (e.g. setup/pid1/f41true:bin/f41true)",
    )
    ap.add_argument(
        "--qemu-arg",
        action="append",
        default=[],
        help="Extra QEMU argument after -drive (repeatable; e.g. --qemu-arg -netdev --qemu-arg user,id=net0)",
    )
    args = ap.parse_args()
    require = list(args.require)
    if not require:
        require = ["TEST_END|fork_wait_signal|PASS"]

    iso = ROOT / "kernel-x64-userspace.iso"
    if not iso.is_file():
        print(f"✗ missing {iso}; build kernel-x64-userspace.iso first", file=sys.stderr)
        return 2

    init_bin = Path(args.init)
    if not init_bin.is_file():
        print(f"✗ missing init {init_bin}; build first", file=sys.stderr)
        return 2

    injects: list[tuple[Path, str]] = []
    for spec in args.inject:
        if ":" not in spec:
            print(f"✗ --inject expects SRC:DEST, got {spec!r}", file=sys.stderr)
            return 2
        src_s, dest = spec.split(":", 1)
        src = Path(src_s)
        if not src.is_file():
            src = ROOT / src_s
        if not src.is_file():
            print(f"✗ --inject missing source: {src_s}", file=sys.stderr)
            return 2
        dest = dest.lstrip("/")
        if not dest:
            print(f"✗ --inject empty DEST in {spec!r}", file=sys.stderr)
            return 2
        injects.append((src, dest))

    base = ROOT / "disk.img"
    if not base.is_file():
        print("✗ missing disk.img", file=sys.stderr)
        return 2

    share_dir: Path | None = None
    own_share = False
    if args.share:
        share_dir = Path(args.share)
        share_dir.mkdir(parents=True, exist_ok=True)
    elif args.host_file:
        share_dir = Path(tempfile.mkdtemp(prefix="ir0-ktm-share-"))
        own_share = True

    log = Path(args.log)
    if log.exists():
        log.unlink()

    disk = Path(tempfile.mktemp(prefix="ir0-ktm-userdev-", suffix=".img"))
    try:
        subprocess.check_call(["cp", "-f", str(base), str(disk)], cwd=str(ROOT))
        subprocess.check_call(
            ["python3", "scripts/inject_init_minix.py", str(disk), str(init_bin), "sbin/init"],
            cwd=str(ROOT),
        )
        for src, dest in injects:
            subprocess.check_call(
                ["python3", "scripts/inject_init_minix.py", str(disk), str(src), dest],
                cwd=str(ROOT),
            )

        smoke = ROOT / "scripts" / "smoke_qemu_run.sh"
        qemu = os.environ.get("QEMU", "qemu-system-x86_64")
        cmd = [
            str(smoke),
            "--log",
            str(log),
            "--timeout",
            str(args.timeout),
            "--done",
            args.done,
            "--",
            qemu,
            "-cdrom",
            str(iso),
            "-drive",
            f"file={disk},format=raw,if=ide,index=0",
        ]
        if share_dir is not None:
            cmd += [
                "-fsdev",
                f"local,id=ir0fs,path={share_dir},security_model=none",
                "-device",
                "virtio-9p-pci,fsdev=ir0fs,mount_tag=ir0share,disable-modern=on",
            ]
        if args.qemu_arg:
            cmd += list(args.qemu_arg)
        has_netdev = any("netdev" in a for a in (args.qemu_arg or []))
        cmd += [
            "-serial",
            "stdio",
            "-display",
            "none",
            "-m",
            "256M",
            "-no-reboot",
        ]
        if not has_netdev:
            cmd += ["-net", "none"]
        print(f"  KTM-USERDEV-RUN log={log}" + (f" share={share_dir}" if share_dir else ""))
        rc = subprocess.call(cmd, cwd=str(ROOT))
    finally:
        if disk.exists():
            disk.unlink()

    text = log.read_text(encoding="utf-8", errors="replace") if log.is_file() else ""
    text = text.replace("\0", "").replace("\r", "")
    flat = text.replace("\n", "")
    if args.done not in flat:
        print(f"✗ {args.done} missing", file=sys.stderr)
        for line in text.splitlines():
            if "KTM|" in line or "KTM_" in line:
                print(line)
        if own_share and share_dir and share_dir.exists():
            subprocess.call(["rm", "-rf", str(share_dir)])
        return 1 if rc == 0 else rc
    for needle in require:
        if needle not in flat:
            print(f"✗ required tag missing: {needle}", file=sys.stderr)
            if own_share and share_dir and share_dir.exists():
                subprocess.call(["rm", "-rf", str(share_dir)])
            return 1

    if args.host_file and share_dir is not None:
        host_path = share_dir / args.host_file
        if not host_path.is_file():
            print(f"✗ host share missing file: {host_path}", file=sys.stderr)
            if own_share:
                subprocess.call(["rm", "-rf", str(share_dir)])
            return 1
        body = host_path.read_text(encoding="utf-8", errors="replace")
        if args.host_grep and args.host_grep not in body:
            print(f"✗ host file missing '{args.host_grep}': {host_path}", file=sys.stderr)
            if own_share:
                subprocess.call(["rm", "-rf", str(share_dir)])
            return 1
        print(f"✓ host share OK ({host_path})")

    if own_share and share_dir and share_dir.exists():
        subprocess.call(["rm", "-rf", str(share_dir)])

    print("✓ ktm-userdev-run OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
