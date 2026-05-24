#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Inject a file into a MINIX v1 disk image without mounting (no minix module).

Usage:
  inject_init_minix.py --format DISK_IMAGE
  inject_init_minix.py DISK_IMAGE FILE [DEST_PATH]

  DEST_PATH: slash-separated path without leading slash (default: sbin/init)
  Examples:
    inject_init_minix.py --format disk.img
    inject_init_minix.py disk.img setup/pid1/init
    inject_init_minix.py disk.img setup/pid1/sh_smoke bin/sh
"""

import os
import struct
import sys

BLOCK = 1024
INODE_SIZE = 32
NAME_LEN = 14
DIR_ENTRY = 16
MAGIC = 0x137F
IFDIR = 0o040000
IFREG = 0o100000
IFMT = 0o170000


def read_block(f, n):
    f.seek(n * BLOCK)
    data = f.read(BLOCK)
    if len(data) < BLOCK:
        data = data.ljust(BLOCK, b"\x00")
    return data


def write_block(f, n, data):
    f.seek(n * BLOCK)
    f.write(data[:BLOCK].ljust(BLOCK, b"\x00"))


def parse_super(sb):
    ninodes, nzones, imap_b, zmap_b, firstdz, logzs, max_size, magic = struct.unpack(
        "<6H I H", sb[:18]
    )
    if magic != MAGIC:
        raise SystemExit(f"not MINIX v1 (magic 0x{magic:04x}, expected 0x{MAGIC:04x})")
    return {
        "ninodes": ninodes,
        "nzones": nzones,
        "imap_blocks": imap_b,
        "zmap_blocks": zmap_b,
        "firstdatazone": firstdz,
        "log_zone_size": logzs,
        "max_size": max_size,
    }


def inode_table_start(sb):
    return 2 + sb["imap_blocks"] + sb["zmap_blocks"]


def read_inode(f, sb, num):
    itab = inode_table_start(sb)
    off = (num - 1) * INODE_SIZE
    blk = itab + off // BLOCK
    bo = off % BLOCK
    raw = read_block(f, blk)[bo : bo + INODE_SIZE]
    mode, uid, size, mtime, gid, nlinks = struct.unpack("<HHIIBB", raw[:14])
    zones = struct.unpack("<9H", raw[14:32])
    return {
        "mode": mode,
        "uid": uid,
        "size": size,
        "mtime": mtime,
        "gid": gid,
        "nlinks": nlinks,
        "zones": list(zones),
        "raw": bytearray(raw),
    }


def write_inode(f, sb, num, inode):
    itab = inode_table_start(sb)
    off = (num - 1) * INODE_SIZE
    blk = itab + off // BLOCK
    bo = off % BLOCK
    data = bytearray(read_block(f, blk))
    zones = inode["zones"] + [0] * 9
    packed = struct.pack(
        "<HHIIBB",
        inode["mode"],
        inode["uid"],
        inode["size"],
        inode["mtime"],
        inode["gid"],
        inode["nlinks"],
    ) + struct.pack("<9H", *zones[:9])
    data[bo : bo + INODE_SIZE] = packed
    write_block(f, blk, bytes(data))


def imap_block(sb):
    return 2


def zmap_start(sb):
    return 2 + sb["imap_blocks"]


def alloc_inode(f, sb):
    imap = read_block(f, imap_block(sb))
    for n in range(1, sb["ninodes"] + 1):
        byte_i = n // 8
        bit_i = n % 8
        if byte_i >= BLOCK:
            break
        if (imap[byte_i] & (1 << bit_i)) == 0:
            imap = bytearray(imap)
            imap[byte_i] |= 1 << bit_i
            write_block(f, imap_block(sb), bytes(imap))
            return n
    raise SystemExit("no free inode")


def zone_free(f, sb, zone):
    if zone < sb["firstdatazone"]:
        return False
    idx = zone - sb["firstdatazone"]
    byte_i = idx // 8
    bit_i = idx % 8
    zmap_blk = zmap_start(sb) + byte_i // BLOCK
    zoff = byte_i % BLOCK
    zmap = bytearray(read_block(f, zmap_blk))
    return (zmap[zoff] & (1 << bit_i)) != 0


def alloc_zone(f, sb):
    zmap_blk_base = zmap_start(sb)
    for zone in range(sb["firstdatazone"], sb["nzones"]):
        if not zone_free(f, sb, zone):
            continue
        idx = zone - sb["firstdatazone"]
        byte_i = idx // 8
        bit_i = idx % 8
        zmap_blk = zmap_blk_base + byte_i // BLOCK
        zoff = byte_i % BLOCK
        zmap = bytearray(read_block(f, zmap_blk))
        zmap[zoff] &= ~(1 << bit_i)
        write_block(f, zmap_blk, bytes(zmap))
        return zone
    raise SystemExit("no free zone")


def dir_entries(f, inode):
    if (inode["mode"] & IFMT) != IFDIR:
        return []
    z = inode["zones"][0]
    if z == 0:
        return []
    raw = read_block(f, z)
    out = []
    for i in range(0, BLOCK, DIR_ENTRY):
        ino, name = struct.unpack("<H", raw[i : i + 2])[0], raw[i + 2 : i + DIR_ENTRY]
        name = name.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        if ino != 0:
            out.append((ino, name))
    return out


def find_in_dir(f, sb, inode, name):
    for ino, n in dir_entries(f, inode):
        if n == name:
            return ino
    return 0


def remove_dir_entry(f, sb, dir_inode, dir_num, name):
    z = dir_inode["zones"][0]
    raw = bytearray(read_block(f, z))
    nb = name.encode("ascii")[:NAME_LEN]
    for i in range(0, BLOCK, DIR_ENTRY):
        ino = struct.unpack("<H", raw[i : i + 2])[0]
        entry_name = raw[i + 2 : i + DIR_ENTRY].split(b"\x00", 1)[0]
        if ino != 0 and entry_name == nb:
            raw[i : i + DIR_ENTRY] = b"\x00" * DIR_ENTRY
            write_block(f, z, bytes(raw))
            if dir_inode["size"] >= DIR_ENTRY:
                dir_inode["size"] -= DIR_ENTRY
            write_inode(f, sb, dir_num, dir_inode)
            return True
    return False


def audit_entry(source_path, dest_path, source_type, mode, inode_num, size):
    print(
        f"MINIX_INJECT source={source_path} dest={dest_path} "
        f"source_type={source_type} mode=0x{mode:04x} inode={inode_num} size={size}"
    )


def detect_source_type(path):
    if not os.path.exists(path):
        return "missing"
    if os.path.isdir(path):
        return "directory"
    if os.path.isfile(path):
        return "file"
    return "other"


def assert_regular_source(source_path, dest_path):
    st = detect_source_type(source_path)
    dest_norm = "/" + dest_path.lstrip("/")
    if dest_norm == "/bin/busybox":
        if st != "file":
            raise SystemExit(
                f"ROOTFS_BUSYBOX_WRONG_TYPE abort: {dest_norm} must come from a "
                f"regular file source, got source_type={st} path={source_path!r}"
            )
    if st == "missing":
        raise SystemExit(f"inject abort: source missing: {source_path}")
    if st == "directory":
        raise SystemExit(
            f"inject abort: source is a directory, not a file: {source_path} -> {dest_path}"
        )
    if st != "file":
        raise SystemExit(f"inject abort: unsupported source type {st}: {source_path}")


def zmap_blocks_for(nzones, firstdatazone):
    data_zones = max(0, nzones - firstdatazone)
    nbytes = (data_zones + 7) // 8
    return max(1, (nbytes + BLOCK - 1) // BLOCK)


def format_minix_v1(f, ninodes=64, nzones=1024):
    """
    Format blank/raw image as MINIX v1 matching kernel minix_fs_format() layout.
    firstdatazone accounts for the inode table size (ninodes * 32 bytes).
    """
    imap_blocks = 1
    inode_table_blocks = (ninodes * INODE_SIZE + BLOCK - 1) // BLOCK
    zmap_blocks = 1
    firstdatazone = 2 + imap_blocks + zmap_blocks + inode_table_blocks
    for _ in range(8):
        zmap_blocks = zmap_blocks_for(nzones, firstdatazone)
        firstdatazone = 2 + imap_blocks + zmap_blocks + inode_table_blocks
    if firstdatazone >= nzones:
        raise SystemExit(
            f"format_minix_v1: firstdatazone={firstdatazone} >= nzones={nzones}"
        )
    sb = {
        "ninodes": ninodes,
        "nzones": nzones,
        "imap_blocks": imap_blocks,
        "zmap_blocks": zmap_blocks,
        "firstdatazone": firstdatazone,
        "log_zone_size": 0,
        "max_size": 1048576,
    }
    sb_block = bytearray(BLOCK)
    struct.pack_into(
        "<6H I H",
        sb_block,
        0,
        sb["ninodes"],
        sb["nzones"],
        sb["imap_blocks"],
        sb["zmap_blocks"],
        sb["firstdatazone"],
        sb["log_zone_size"],
        sb["max_size"],
        MAGIC,
    )
    write_block(f, 1, bytes(sb_block))

    imap = bytearray(BLOCK)
    imap[0] = 0x02
    write_block(f, 2, bytes(imap))

    zmap_base = 2 + imap_blocks
    for blk in range(zmap_blocks):
        zmap = bytearray(BLOCK)
        for i in range(BLOCK):
            zmap[i] = 0xFF
        if blk == 0:
            zmap[0] &= ~(1 << 0)
        write_block(f, zmap_base + blk, bytes(zmap))

    root_zone = sb["firstdatazone"]
    root_inode = {
        "mode": IFDIR | 0o755,
        "uid": 0,
        "size": 2 * DIR_ENTRY,
        "mtime": 0,
        "gid": 0,
        "nlinks": 2,
        "zones": [root_zone] + [0] * 8,
    }
    write_inode(f, sb, 1, root_inode)

    root_dir = bytearray(BLOCK)
    struct.pack_into("<H", root_dir, 0, 1)
    root_dir[2:3] = b"."
    struct.pack_into("<H", root_dir, DIR_ENTRY, 1)
    root_dir[DIR_ENTRY + 2 : DIR_ENTRY + 4] = b".."
    write_block(f, root_zone, bytes(root_dir))

    print(
        f"MINIX_FORMAT ok magic=0x{MAGIC:04x} ninodes={sb['ninodes']} "
        f"nzones={sb['nzones']} firstdatazone={sb['firstdatazone']}"
    )
    return sb


def add_dir_entry(f, sb, dir_inode, dir_num, name, child_num):
    z = dir_inode["zones"][0]
    raw = bytearray(read_block(f, z))
    for i in range(0, BLOCK, DIR_ENTRY):
        ino = struct.unpack("<H", raw[i : i + 2])[0]
        if ino != 0:
            continue
        nb = name.encode("ascii")[:NAME_LEN]
        raw[i : i + 2] = struct.pack("<H", child_num)
        raw[i + 2 : i + 2 + len(nb)] = nb
        write_block(f, z, bytes(raw))
        dir_inode["size"] += DIR_ENTRY
        write_inode(f, sb, dir_num, dir_inode)
        return
    raise SystemExit(f"directory full: cannot add {name}")


def mkdir(f, sb, parent_num, parent, name, parent_prefix):
    existing = find_in_dir(f, sb, parent, name)
    if existing:
        return existing, read_inode(f, sb, existing)
    num = alloc_inode(f, sb)
    zone = alloc_zone(f, sb)
    mode = IFDIR | 0o755
    child = {
        "mode": mode,
        "uid": 0,
        "size": 2 * DIR_ENTRY,
        "mtime": 0,
        "gid": 0,
        "nlinks": 2,
        "zones": [zone] + [0] * 8,
    }
    write_inode(f, sb, num, child)
    block = bytearray(BLOCK)
    struct.pack_into("<H", block, 0, num)
    block[2:3] = b"."
    struct.pack_into("<H", block, DIR_ENTRY, parent_num)
    block[DIR_ENTRY + 2 : DIR_ENTRY + 4] = b".."
    write_block(f, zone, bytes(block))
    audit_entry(
        "(mkdir)",
        f"{parent_prefix}/{name}" if parent_prefix != "/" else f"/{name}",
        "directory",
        mode,
        num,
        child["size"],
    )
    add_dir_entry(f, sb, parent, parent_num, name, num)
    parent["nlinks"] += 1
    write_inode(f, sb, parent_num, parent)
    return num, child


def prepare_regular_file(f, sb, file_inode, data):
    zones_needed = (len(data) + BLOCK - 1) // BLOCK
    entries_per_block = BLOCK // 2
    max_blocks = 7 + entries_per_block + (entries_per_block * entries_per_block)
    if zones_needed > max_blocks:
        raise SystemExit(f"file too large for MINIX v1 ({len(data)} bytes)")

    data_zones = []
    for c in range(zones_needed):
        if c < 7 and c < len(file_inode["zones"]) and file_inode["zones"][c]:
            data_zones.append(file_inode["zones"][c])
        else:
            data_zones.append(alloc_zone(f, sb))

    for c, z in enumerate(data_zones):
        chunk = data[c * BLOCK : (c + 1) * BLOCK]
        write_block(f, z, chunk.ljust(BLOCK, b"\x00"))

    zones = [0] * 9
    for c in range(min(7, zones_needed)):
        zones[c] = data_zones[c]
    if zones_needed > 7:
        ind_blk = bytearray(BLOCK)
        single_count = min(entries_per_block, zones_needed - 7)
        ind_zone = file_inode["zones"][7] if file_inode["zones"][7] else alloc_zone(f, sb)
        for i in range(single_count):
            struct.pack_into("<H", ind_blk, i * 2, data_zones[7 + i])
        write_block(f, ind_zone, bytes(ind_blk))
        zones[7] = ind_zone

        remaining = zones_needed - 7 - single_count
        if remaining > 0:
            dind_blk = bytearray(BLOCK)
            dind_zone = file_inode["zones"][8] if file_inode["zones"][8] else alloc_zone(f, sb)
            offset = 7 + single_count
            block_idx = 0

            while remaining > 0:
                chunk = min(entries_per_block, remaining)
                lvl1_blk = bytearray(BLOCK)
                lvl1_zone = alloc_zone(f, sb)
                for i in range(chunk):
                    struct.pack_into("<H", lvl1_blk, i * 2, data_zones[offset + i])
                write_block(f, lvl1_zone, bytes(lvl1_blk))
                struct.pack_into("<H", dind_blk, block_idx * 2, lvl1_zone)
                block_idx += 1
                offset += chunk
                remaining -= chunk

            write_block(f, dind_zone, bytes(dind_blk))
            zones[8] = dind_zone

    file_inode["mode"] = IFREG | 0o755
    file_inode["size"] = len(data)
    file_inode["zones"] = zones
    file_inode["nlinks"] = 1
    return file_inode


def write_file(f, sb, path_parts, data, source_path):
    root = read_inode(f, sb, 1)
    cur_num = 1
    cur = root
    dest_prefix = ""
    for i, part in enumerate(path_parts):
        is_last = i == len(path_parts) - 1
        if is_last:
            dest_path = "/" + "/".join(path_parts)
            ino = find_in_dir(f, sb, cur, part)
            new_entry = False
            if ino != 0:
                existing = read_inode(f, sb, ino)
                if (existing["mode"] & IFMT) == IFDIR:
                    remove_dir_entry(f, sb, cur, cur_num, part)
                    ino = 0
            if ino == 0:
                ino = alloc_inode(f, sb)
                new_entry = True
                file_inode = {
                    "mode": IFREG | 0o755,
                    "uid": 0,
                    "size": 0,
                    "mtime": 0,
                    "gid": 0,
                    "nlinks": 1,
                    "zones": [0] * 9,
                }
            else:
                file_inode = read_inode(f, sb, ino)
                if (file_inode["mode"] & IFMT) != IFREG:
                    file_inode = {
                        "mode": IFREG | 0o755,
                        "uid": 0,
                        "size": 0,
                        "mtime": 0,
                        "gid": 0,
                        "nlinks": 1,
                        "zones": [0] * 9,
                    }

            file_inode = prepare_regular_file(f, sb, file_inode, data)
            audit_entry(
                source_path,
                dest_path,
                "file",
                file_inode["mode"],
                ino,
                file_inode["size"],
            )
            write_inode(f, sb, ino, file_inode)
            if new_entry:
                add_dir_entry(f, sb, cur, cur_num, part, ino)
            return

        dest_prefix = dest_prefix + "/" + part if dest_prefix else "/" + part
        ino = find_in_dir(f, sb, cur, part)
        if ino == 0:
            parent_audit = dest_prefix.rsplit("/", 1)[0] or "/"
            ino, cur = mkdir(f, sb, cur_num, cur, part, parent_audit)
            cur_num = ino
        else:
            cur_num = ino
            cur = read_inode(f, sb, ino)


def main():
    if len(sys.argv) >= 2 and sys.argv[1] in ("--format", "--format-large"):
        if len(sys.argv) != 3:
            print(
                f"Usage: {sys.argv[0]} --format DISK_IMAGE",
                file=sys.stderr,
            )
            print(
                f"       {sys.argv[0]} --format-large DISK_IMAGE",
                file=sys.stderr,
            )
            sys.exit(1)
        disk_path = sys.argv[2]
        with open(disk_path, "r+b") as f:
            if sys.argv[1] == "--format-large":
                format_minix_v1(f, ninodes=256, nzones=65535)
            else:
                format_minix_v1(f)
        print(f"✅ Formatted {disk_path} as MINIX v1 (kernel-compatible layout)")
        return

    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print(
            f"Usage: {sys.argv[0]} --format DISK_IMAGE",
            file=sys.stderr,
        )
        print(
            f"       {sys.argv[0]} --format-large DISK_IMAGE",
            file=sys.stderr,
        )
        print(
            f"       {sys.argv[0]} DISK_IMAGE FILE [DEST_PATH]",
            file=sys.stderr,
        )
        print("  DEST_PATH default: sbin/init (e.g. bin/sh)", file=sys.stderr)
        sys.exit(1)

    disk_path = sys.argv[1]
    file_path = sys.argv[2]
    dest = sys.argv[3] if len(sys.argv) == 4 else "sbin/init"
    path_parts = [p for p in dest.split("/") if p]
    if not path_parts:
        print("invalid DEST_PATH", file=sys.stderr)
        sys.exit(1)

    assert_regular_source(file_path, dest)

    with open(file_path, "rb") as inf:
        data = inf.read()

    with open(disk_path, "r+b") as f:
        sb = parse_super(read_block(f, 1))
        write_file(f, sb, path_parts, data, file_path)

    dest_display = "/" + "/".join(path_parts)
    print(
        f"✅ Injected {file_path} -> {disk_path}:{dest_display} "
        f"({len(data)} bytes, no mount)"
    )


if __name__ == "__main__":
    main()
