#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host runner for KTM /dev/ktm userspace pilots via virtio-9p payload exec."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

# Scripts live next to this file; keep the import working from any cwd.
sys.path.insert(0, str(Path(__file__).resolve().parent))

from ktm_host_safety import (  # noqa: E402  (after sys.path setup)
    MAX_LOG_BYTES,
    MAX_MEM_MIB,
    MAX_TIMEOUT_SEC,
    make_temp_disk,
    parse_mem_mib,
    scan_log,
)

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_STUB = ROOT / "setup/pid1/init_hostshare_exec"
PAYLOAD_NAME = "ir0_payload"
DEFAULT_LOG = Path(tempfile.gettempdir()) / "ktm-userdev-run.log"


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Run IR0 KTM userdev pilot: stub on disk + payload on virtio-9p"
    )
    ap.add_argument("--log", default=str(DEFAULT_LOG))
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument(
        "--mem",
        default="256M",
        help="QEMU guest RAM (default 256M; capped at 2G or 25% of host RAM)",
    )
    ap.add_argument(
        "--init",
        default=str(ROOT / "tests/ktm/userdev/ktm_fork_wait_case"),
        help="Userspace payload ELF (placed on share as ir0_payload)",
    )
    ap.add_argument(
        "--stub",
        default=str(DEFAULT_STUB),
        help="MINIX /sbin/init stub that mounts 9p and execve payload",
    )
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
        help="Host directory for QEMU virtio-9p (mount_tag=ir0share).",
    )
    ap.add_argument(
        "--host-file",
        default="",
        help="Relative path under share that must exist after the smoke",
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
        help="Extra inject into MINIX disk (not the payload)",
    )
    ap.add_argument(
        "--qemu-arg",
        action="append",
        default=[],
        help="Extra QEMU argument after -drive (repeatable)",
    )
    ap.add_argument(
        "--legacy-disk-init",
        action="store_true",
        help="Inject --init as /sbin/init (old path; no share payload)",
    )
    ap.add_argument(
        "--disk",
        default="",
        help="Prebuilt MINIX disk (e.g. packed ISD). Copied before "
        "--legacy-disk-init/--inject so the source image is not mutated.",
    )
    ap.add_argument(
        "--no-fsdev",
        action="store_true",
        help="Omit virtio-9p share (product disks that do not need hostshare)",
    )
    args = ap.parse_args()
    require = list(args.require)
    if not require:
        require = ["TEST_END|fork_wait_signal|PASS"]

    if not (1 <= args.timeout <= MAX_TIMEOUT_SEC):
        print(f"✗ --timeout must be 1..{MAX_TIMEOUT_SEC}", file=sys.stderr)
        return 2
    try:
        mem_mib = parse_mem_mib(args.mem)
    except ValueError as exc:
        print(f"✗ {exc}", file=sys.stderr)
        return 2
    if mem_mib > MAX_MEM_MIB:
        print(
            f"✗ --mem {mem_mib} MiB exceeds the {MAX_MEM_MIB} MiB host-safe cap",
            file=sys.stderr,
        )
        return 2

    iso = ROOT / "kernel-x64-userspace.iso"
    if not iso.is_file():
        print(f"✗ missing {iso}; build kernel-x64-userspace.iso first", file=sys.stderr)
        return 2

    payload_bin = Path(args.init)
    if not payload_bin.is_file():
        print(f"✗ missing payload {payload_bin}; build first", file=sys.stderr)
        return 2

    stub_bin = Path(args.stub)
    prebuilt_disk = Path(args.disk) if args.disk else None
    if prebuilt_disk is not None:
        if not prebuilt_disk.is_file():
            print(f"✗ missing --disk {prebuilt_disk}", file=sys.stderr)
            return 2
    elif not args.legacy_disk_init and not stub_bin.is_file():
        print(f"✗ missing stub {stub_bin}; make build-init-hostshare-exec", file=sys.stderr)
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
        if not dest or ".." in dest.split("/"):
            print(f"✗ --inject DEST must stay inside the image: {spec!r}", file=sys.stderr)
            return 2
        injects.append((src, dest))

    base = ROOT / "disk.img"
    if prebuilt_disk is None and not base.is_file():
        print("✗ missing disk.img", file=sys.stderr)
        return 2

    share_dir: Path | None = None
    own_share = False
    if not args.no_fsdev:
        if args.share:
            share_dir = Path(args.share)
            share_dir.mkdir(parents=True, exist_ok=True)
        else:
            share_dir = Path(tempfile.mkdtemp(prefix="ir0-ktm-share-"))
            own_share = True

        # Always place --init on the 9p share as ir0_payload (stub PID1 or
        # runit supervised hostshare service both exec this path).
        if not args.legacy_disk_init:
            shutil.copy2(payload_bin, share_dir / PAYLOAD_NAME)
            os.chmod(share_dir / PAYLOAD_NAME, 0o755)

    log = Path(args.log)
    if log.exists():
        log.unlink()

    mutate = bool(args.legacy_disk_init or injects)
    own_disk = False
    if prebuilt_disk is not None:
        if mutate:
            disk = make_temp_disk()
            own_disk = True
        else:
            disk = prebuilt_disk
    else:
        disk = make_temp_disk()
        own_disk = True

    try:
        if prebuilt_disk is not None and own_disk:
            shutil.copy2(prebuilt_disk, disk)
        elif prebuilt_disk is None:
            shutil.copy2(base, disk)
        # Replace PID1 on a work copy: empty kernel disk.img (stub or payload)
        # or a packed ISD disk (--legacy-disk-init runs the case as /sbin/init).
        if prebuilt_disk is None or args.legacy_disk_init:
            disk_init = payload_bin if args.legacy_disk_init else stub_bin
            subprocess.check_call(
                [
                    sys.executable,
                    "scripts/inject_init_minix.py",
                    str(disk),
                    str(disk_init),
                    "sbin/init",
                ],
                cwd=str(ROOT),
            )
        # Extra --inject applies to the work copy (never the caller's --disk).
        for src, dest in injects:
            subprocess.check_call(
                [
                    sys.executable,
                    "scripts/inject_init_minix.py",
                    str(disk),
                    str(src),
                    dest,
                ],
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
            args.mem,
            "-no-reboot",
        ]
        if not has_netdev:
            cmd += ["-net", "none"]
        if prebuilt_disk is not None:
            mode = f"prebuilt-disk={disk.name}"
        elif args.legacy_disk_init:
            mode = "legacy-disk-init"
        else:
            mode = f"share-payload={PAYLOAD_NAME}"
        print(f"  KTM-USERDEV-RUN log={log} share={share_dir} ({mode})")
        rc = subprocess.call(cmd, cwd=str(ROOT))

        if log.is_file() and log.stat().st_size > MAX_LOG_BYTES:
            print(
                f"✗ serial log exceeds {MAX_LOG_BYTES // (1024 * 1024)} MiB; "
                "the guest is likely looping",
                file=sys.stderr,
            )
            return 1

        needles = [args.done, *require]
        found, matches = scan_log(log, needles)
        if args.done not in found:
            print(f"✗ {args.done} missing", file=sys.stderr)
            for line in matches:
                print(line)
            return 1 if rc == 0 else rc
        for needle in require:
            if needle not in found:
                print(f"✗ required tag missing: {needle}", file=sys.stderr)
                return 1

        if args.host_file and share_dir is not None:
            share_root = share_dir.resolve()
            host_path = (share_dir / args.host_file).resolve()
            try:
                host_path.relative_to(share_root)
            except ValueError:
                print(
                    f"✗ --host-file escapes the share: {args.host_file}",
                    file=sys.stderr,
                )
                return 1
            if not host_path.is_file():
                print(f"✗ host share missing file: {host_path}", file=sys.stderr)
                return 1
            body = host_path.read_text(encoding="utf-8", errors="replace")
            if args.host_grep and args.host_grep not in body:
                print(
                    f"✗ host file missing '{args.host_grep}': {host_path}",
                    file=sys.stderr,
                )
                return 1
            print(f"✓ host share OK ({host_path})")

        print("✓ ktm-userdev-run OK")
        return 0
    finally:
        if own_disk and disk.exists():
            disk.unlink()
        if own_share and share_dir and share_dir.exists():
            shutil.rmtree(share_dir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
