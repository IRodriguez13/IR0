#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Migrate a hidden legacy MINIX /home into the persistent ext2 home disk."""

from __future__ import annotations

import argparse
import importlib.util
import os
import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


def load_minix_module(root: Path):
    path = root / "scripts/inject_init_minix.py"
    spec = importlib.util.spec_from_file_location("ir0_minix", path)
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def inode_data(module, stream, inode: dict) -> bytes:
    zones = list(inode["zones"][:7])
    if inode["zones"][7]:
        raw = module.read_block(stream, inode["zones"][7])
        zones.extend(struct.unpack("<512H", raw))
    if inode["zones"][8]:
        outer = module.read_block(stream, inode["zones"][8])
        for indirect in struct.unpack("<512H", outer):
            if indirect:
                zones.extend(struct.unpack("<512H", module.read_block(stream, indirect)))
    data = b"".join(module.read_block(stream, zone) for zone in zones if zone)
    return data[: inode["size"]]


def debugfs(image: Path, command: str) -> None:
    result = subprocess.run(
        ["debugfs", "-w", "-R", command, str(image)],
        text=True,
        capture_output=True,
        check=False,
    )
    if result.returncode or "File not found" in result.stderr:
        raise RuntimeError(f"debugfs {command!r}: {result.stderr.strip()}")


def set_metadata(image: Path, path: str, inode: dict) -> None:
    debugfs(image, f"set_inode_field {path} uid {inode['uid']}")
    debugfs(image, f"set_inode_field {path} gid {inode['gid']}")
    debugfs(image, f"set_inode_field {path} mode {inode['mode']}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--minix", required=True, type=Path)
    parser.add_argument("--ext2", required=True, type=Path)
    parser.add_argument("--kernel-root", required=True, type=Path)
    args = parser.parse_args()
    module = load_minix_module(args.kernel_root)

    with args.minix.open("rb") as source, tempfile.TemporaryDirectory() as staging_name:
        superblock = module.parse_super(module.read_block(source, 1))
        inode_number = 1
        for component in ("home", "ivan"):
            parent = module.read_inode(source, superblock, inode_number)
            inode_number = module.find_in_dir(source, superblock, parent, component)
            if not inode_number:
                raise RuntimeError("legacy /home/ivan was not found")

        staging = Path(staging_name)

        def migrate(number: int, destination: str) -> None:
            inode = module.read_inode(source, superblock, number)
            kind = inode["mode"] & module.IFMT
            if kind == module.IFDIR:
                debugfs(args.ext2, f"mkdir {destination}")
                set_metadata(args.ext2, destination, inode)
                for child, name in module.dir_entries(source, inode):
                    if name not in (".", ".."):
                        migrate(child, f"{destination}/{name}")
            elif kind == module.IFREG:
                host_file = staging / f"inode-{number}"
                host_file.write_bytes(inode_data(module, source, inode))
                debugfs(args.ext2, f"write {host_file} {destination}")
                set_metadata(args.ext2, destination, inode)

        migrate(inode_number, "/ivan")

    print("✓ migrated legacy /home/ivan from MINIX to ext2")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
