#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Inject a file into a MINIX v1 disk image without mounting (no minix module).

Usage:
  inject_init_minix.py DISK_IMAGE FILE [DEST_PATH]

  DEST_PATH: slash-separated path without leading slash (default: sbin/init)
  Examples:
    inject_init_minix.py disk.img setup/pid1/init
    inject_init_minix.py disk.img setup/pid1/sh_smoke bin/sh
"""

import struct
import sys

BLOCK = 1024
INODE_SIZE = 32
NAME_LEN = 14
DIR_ENTRY = 16
MAGIC = 0x137F
IFDIR = 0o040000
IFREG = 0o100000


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
    if (inode["mode"] & 0o170000) != IFDIR:
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


def mkdir(f, sb, parent_num, parent, name):
    existing = find_in_dir(f, sb, parent, name)
    if existing:
        return existing, read_inode(f, sb, existing)
    num = alloc_inode(f, sb)
    zone = alloc_zone(f, sb)
    child = {
        "mode": IFDIR | 0o755,
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
    add_dir_entry(f, sb, parent, parent_num, name, num)
    parent["nlinks"] += 1
    write_inode(f, sb, parent_num, parent)
    return num, child


def write_file(f, sb, path_parts, data):
    root = read_inode(f, sb, 1)
    cur_num = 1
    cur = root
    for i, part in enumerate(path_parts):
        is_last = i == len(path_parts) - 1
        if is_last:
            ino = find_in_dir(f, sb, cur, part)
            if ino != 0:
                existing = read_inode(f, sb, ino)
                if (existing["mode"] & 0o170000) == IFDIR:
                    remove_dir_entry(f, sb, cur, cur_num, part)
                    ino = 0
            if ino == 0:
                ino = alloc_inode(f, sb)
                add_dir_entry(f, sb, cur, cur_num, part, ino)
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
                if (file_inode["mode"] & 0o170000) != IFREG:
                    file_inode = {
                        "mode": IFREG | 0o755,
                        "uid": 0,
                        "size": 0,
                        "mtime": 0,
                        "gid": 0,
                        "nlinks": 1,
                        "zones": [0] * 9,
                    }
            zones_needed = (len(data) + BLOCK - 1) // BLOCK
            max_blocks = 7 + (BLOCK // 2)
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
                ind_zone = file_inode["zones"][7] if file_inode["zones"][7] else alloc_zone(f, sb)
                for c in range(7, zones_needed):
                    struct.pack_into("<H", ind_blk, (c - 7) * 2, data_zones[c])
                write_block(f, ind_zone, bytes(ind_blk))
                zones[7] = ind_zone

            file_inode["mode"] = IFREG | 0o755
            file_inode["size"] = len(data)
            file_inode["zones"] = zones
            file_inode["nlinks"] = 1
            write_inode(f, sb, ino, file_inode)
            return
        ino = find_in_dir(f, sb, cur, part)
        if ino == 0:
            ino, cur = mkdir(f, sb, cur_num, cur, part)
            cur_num = ino
        else:
            cur_num = ino
            cur = read_inode(f, sb, ino)


def main():
    if len(sys.argv) < 3 or len(sys.argv) > 4:
        print(
            f"Usage: {sys.argv[0]} DISK_IMAGE FILE [DEST_PATH]",
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
    with open(file_path, "rb") as inf:
        data = inf.read()
    with open(disk_path, "r+b") as f:
        sb = parse_super(read_block(f, 1))
        write_file(f, sb, path_parts, data)
    dest_display = "/" + "/".join(path_parts)
    print(
        f"✅ Injected {file_path} -> {disk_path}:{dest_display} "
        f"({len(data)} bytes, no mount)"
    )


if __name__ == "__main__":
    main()
