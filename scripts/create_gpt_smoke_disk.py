#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a minimal GPT disk image for smoke-gpt-partition (hdb)."""

from __future__ import annotations

import argparse
import struct
import zlib
from pathlib import Path


SECTOR = 512
DISK_SECTORS = 2048  # 1 MiB


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("output", nargs="?", default="build/gpt_smoke.img")
    args = ap.parse_args()
    out = Path(args.output)
    out.parent.mkdir(parents=True, exist_ok=True)

    disk = bytearray(DISK_SECTORS * SECTOR)

    # Protective MBR
    mbr = bytearray(SECTOR)
    # one protective partition type 0xEE covering the disk
    # boot=0, type=0xEE, start_lba=1, sectors=DISK_SECTORS-1
    part = bytearray(16)
    part[4] = 0xEE
    struct.pack_into("<I", part, 8, 1)
    struct.pack_into("<I", part, 12, DISK_SECTORS - 1)
    mbr[446:462] = part
    mbr[510:512] = b"\x55\xAA"
    disk[0:SECTOR] = mbr

    # Partition entry array at LBA 2 (32 entries * 128 = 4096 bytes = 8 sectors)
    entry_lba = 2
    num_entries = 128
    entry_size = 128
    entries = bytearray(num_entries * entry_size)
    # Linux filesystem GUID: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 (mixed endian)
    type_guid = bytes(
        [
            0xAF,
            0x3D,
            0xC6,
            0x0F,
            0x83,
            0x84,
            0x72,
            0x47,
            0x8E,
            0x79,
            0x3D,
            0x69,
            0xD8,
            0x47,
            0x7D,
            0xE4,
        ]
    )
    unique_guid = bytes(range(16))
    first_lba = 34
    last_lba = DISK_SECTORS - 34
    entries[0:16] = type_guid
    entries[16:32] = unique_guid
    struct.pack_into("<QQ", entries, 32, first_lba, last_lba)
    entries_crc = crc32(entries)

    # GPT header at LBA 1
    header = bytearray(SECTOR)
    header[0:8] = b"EFI PART"
    struct.pack_into("<I", header, 8, 0x00010000)  # revision
    struct.pack_into("<I", header, 12, 92)  # header size
    # crc32 at offset 16 — zero while computing
    struct.pack_into("<Q", header, 24, 1)  # current LBA
    struct.pack_into("<Q", header, 32, DISK_SECTORS - 1)  # backup LBA
    struct.pack_into("<Q", header, 40, 34)  # first usable
    struct.pack_into("<Q", header, 48, DISK_SECTORS - 34)  # last usable
    header[56:72] = unique_guid  # disk GUID
    struct.pack_into("<Q", header, 72, entry_lba)
    struct.pack_into("<I", header, 80, num_entries)
    struct.pack_into("<I", header, 84, entry_size)
    struct.pack_into("<I", header, 88, entries_crc)
    hdr_crc = crc32(header[:92])
    struct.pack_into("<I", header, 16, hdr_crc)

    disk[SECTOR : 2 * SECTOR] = header
    disk[entry_lba * SECTOR : entry_lba * SECTOR + len(entries)] = entries

    # Payload in first partition for optional future use
    payload = b"GPT-SMOKE-OK\n"
    disk[first_lba * SECTOR : first_lba * SECTOR + len(payload)] = payload

    out.write_bytes(disk)
    print(f"✓ GPT smoke disk ready: {out} ({DISK_SECTORS} sectors)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
