#!/usr/bin/env python3
"""Build a bounded newc archive for bpivot guest validation."""

from __future__ import annotations

import argparse
from pathlib import Path
import stat
import sys


def align4(value: int) -> int:
    return (value + 3) & ~3


def member(name: str, mode: int, data: bytes, ino: int) -> bytes:
    encoded = name.encode("utf-8") + b"\0"
    fields = (
        ino,
        mode,
        0,
        0,
        1,
        0,
        len(data),
        0,
        0,
        0,
        0,
        len(encoded),
        0,
    )
    header = b"070701" + b"".join(f"{value:08x}".encode("ascii") for value in fields)
    result = bytearray(header)
    result.extend(encoded)
    result.extend(b"\0" * (align4(len(result)) - len(result)))
    result.extend(data)
    result.extend(b"\0" * (align4(len(result)) - len(result)))
    return bytes(result)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    init = args.init.resolve()
    output = args.output.resolve()
    if not init.is_file():
        parser.error(f"missing init binary: {init}")
    data = init.read_bytes()
    if len(data) > 96 * 1024:
        parser.error("init binary exceeds bounded 96 KiB bpivot fixture")

    entries = (
        ("init", stat.S_IFREG | 0o755, data),
        ("sbin", stat.S_IFDIR | 0o755, b""),
        ("sbin/init", stat.S_IFREG | 0o755, data),
        ("etc", stat.S_IFDIR | 0o755, b""),
        ("etc/pivot-probe", stat.S_IFREG | 0o644, b"bpivot-arm64\n"),
        ("TRAILER!!!", 0, b""),
    )
    archive = bytearray()
    for ino, (name, mode, payload) in enumerate(entries, start=1):
        archive.extend(member(name, mode, payload, ino))
    archive.extend(b"\0" * ((512 - len(archive) % 512) % 512))
    if len(archive) > 128 * 1024:
        parser.error("archive exceeds ARM early-rootfs 128 KiB buffer")

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(archive)
    return 0


if __name__ == "__main__":
    sys.exit(main())
