#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Run one IR0 userspace selftest under QEMU with bounded host resources."""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def run(argv: list[str], timeout: int) -> int:
    return subprocess.run(argv, cwd=str(ROOT), check=False, timeout=timeout).returncode


def require(path: Path, hint: str) -> None:
    if not path.exists():
        raise SystemExit(f"ir0-selftest: missing {path} ({hint})")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--arch", choices=("x86_64", "arm64"), required=True)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--timeout", type=int, default=90)
    parser.add_argument(
        "--done",
        default="",
        help="KTAP success tag (default from binary basename)",
    )
    args = parser.parse_args()

    binary = Path(args.binary)
    log = Path(args.log)
    require(binary, "build the selftest first")
    log.parent.mkdir(parents=True, exist_ok=True)
    if log.exists():
        log.unlink()

    smoke = ROOT / "scripts" / "smoke_qemu_run.sh"
    tap = ROOT / "scripts" / "ir0_selftest_tap.py"
    require(smoke, "host QEMU runner")
    require(tap, "KTAP parser")

    done_tag = args.done
    if not done_tag:
        name = binary.name
        if name == "identity":
            done_tag = "ok 2 write_stdout"
        else:
            done_tag = "ok 4 close_ebadf"

    if args.arch == "x86_64":
        iso = ROOT / "kernel-x64-userspace.iso"
        require(iso, "make kernel-x64-userspace.iso")
        fd, disk_name = tempfile.mkstemp(
            prefix="ir0-selftest-x86.", suffix=".img", dir=os.environ.get("TMPDIR", "/tmp")
        )
        os.close(fd)
        disk = Path(disk_name)
        try:
            with disk.open("wb") as fp:
                fp.truncate(32 * 1024 * 1024)
            if run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "inject_init_minix.py"),
                    "--format",
                    str(disk),
                ],
                timeout=30,
            ) != 0:
                return 1
            if run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "inject_init_minix.py"),
                    str(disk),
                    str(binary),
                    "sbin/init",
                ],
                timeout=30,
            ) != 0:
                return 1
            rc = run(
                [
                    str(smoke),
                    "--log",
                    str(log),
                    "--timeout",
                    str(args.timeout),
                    "--stale-sec",
                    "12",
                    "--mem",
                    "256M",
                    "--done",
                    done_tag,
                    "--",
                    "qemu-system-x86_64",
                    "-cdrom",
                    str(iso),
                    "-drive",
                    f"file={disk},format=raw,if=ide,index=0",
                    "-serial",
                    "stdio",
                    "-display",
                    "none",
                    "-no-reboot",
                    "-net",
                    "none",
                ],
                timeout=args.timeout + 30,
            )
        finally:
            disk.unlink(missing_ok=True)
    else:
        product = ROOT / "kernel-arm64-product.img"
        if not product.is_file():
            product = ROOT / "kernel-arm64-product.bin"
        require(product, "ARCH=arm64 make kernel-arm64-product.bin")
        cpio_fd, cpio_name = tempfile.mkstemp(
            prefix="ir0-selftest-arm64.", dir=os.environ.get("TMPDIR", "/tmp")
        )
        os.close(cpio_fd)
        cpio = Path(cpio_name)
        try:
            if run(
                [
                    sys.executable,
                    str(ROOT / "scripts" / "make_test_cpio.py"),
                    "--init",
                    str(binary),
                    "--output",
                    str(cpio),
                ],
                timeout=30,
            ) != 0:
                return 1
            rc = run(
                [
                    str(smoke),
                    "--log",
                    str(log),
                    "--timeout",
                    str(args.timeout),
                    "--stale-sec",
                    "20",
                    "--mem",
                    "256M",
                    "--expected-panic",
                    "--success-mode",
                    "all",
                    "--done",
                    "ARM64_CPIO_ROOT_OK",
                    "--done",
                    "IR0_BPIVOT_COMMIT_OK",
                    "--done",
                    "INIT_EXEC_LOADED_OK",
                    "--done",
                    done_tag,
                    "--",
                    "qemu-system-aarch64",
                    "-M",
                    "virt,gic-version=2",
                    "-cpu",
                    "cortex-a57",
                    "-global",
                    "virtio-mmio.force-legacy=false",
                    "-kernel",
                    str(product),
                    "-nographic",
                    "-serial",
                    "mon:stdio",
                    "-drive",
                    f"file={cpio},format=raw,if=none,id=selftest",
                    "-device",
                    "virtio-blk-device,drive=selftest",
                    "-display",
                    "none",
                    "-no-reboot",
                ],
                timeout=args.timeout + 30,
            )
        finally:
            cpio.unlink(missing_ok=True)

    if rc != 0:
        print(f"ir0-selftest: QEMU failed rc={rc} log={log}", file=sys.stderr)
        return rc
    return run([sys.executable, str(tap), str(log)], timeout=15)


if __name__ == "__main__":
    raise SystemExit(main())
