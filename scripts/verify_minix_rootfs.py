#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Offline MINIX v1 rootfs verification (no mount, no kernel).

Usage:
  verify_minix_rootfs.py DISK_IMAGE [PATH ...]

Default paths when none given: /bin /bin/busybox

Exits 0 on success; prints ROOTFS_* classification tags on failure.
"""

import struct
import sys

BLOCK = 1024
INODE_SIZE = 32
NAME_LEN = 14
DIR_ENTRY = 16
MAGIC = 0x137F
IFMT = 0o170000
IFDIR = 0o040000
IFREG = 0o100000
ELF_MAGIC = b"\x7fELF"
MIN_BUSYBOX_SIZE = 4096


def read_block(f, n):
    f.seek(n * BLOCK)
    data = f.read(BLOCK)
    if len(data) < BLOCK:
        data = data.ljust(BLOCK, b"\x00")
    return data


def parse_super(sb):
    ninodes, nzones, imap_b, zmap_b, firstdz, logzs, max_size, magic = struct.unpack(
        "<6H I H", sb[:18]
    )
    if magic != MAGIC:
        raise SystemExit(
            f"ROOTFS_VERIFY_FAIL not MINIX v1 (magic 0x{magic:04x}, expected 0x{MAGIC:04x})"
        )
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
    }


def dir_entries(f, inode):
    if (inode["mode"] & IFMT) != IFDIR:
        return []
    z = inode["zones"][0]
    if z == 0:
        return []
    raw = read_block(f, z)
    out = []
    for i in range(0, BLOCK, DIR_ENTRY):
        ino, = struct.unpack("<H", raw[i : i + 2])
        if ino == 0:
            continue
        name = raw[i + 2 : i + DIR_ENTRY].split(b"\x00", 1)[0].decode(
            "ascii", errors="replace"
        )
        out.append((ino, name))
    return out


def resolve_path(f, sb, path):
    parts = [p for p in path.split("/") if p]
    if not parts:
        return None, "empty path"
    cur_num = 1
    cur = read_inode(f, sb, 1)
    for i, part in enumerate(parts):
        ino = 0
        for child_ino, name in dir_entries(f, cur):
            if name == part:
                ino = child_ino
                break
        if ino == 0:
            return None, f"missing component '{part}'"
        if i == len(parts) - 1:
            return read_inode(f, sb, ino), None
        if (read_inode(f, sb, ino)["mode"] & IFMT) != IFDIR:
            return None, f"'{part}' is not a directory"
        cur_num = ino
        cur = read_inode(f, sb, ino)
    return None, "unreachable"


def read_file_prefix(f, sb, inode, length):
    if (inode["mode"] & IFMT) != IFREG:
        return b""
    size = min(length, inode["size"])
    if size == 0:
        return b""
    zones = inode["zones"]
    out = bytearray()
    for zidx in range(9):
        if len(out) >= size:
            break
        if zidx == 7 and zones[7]:
            ind = read_block(f, zones[7])
            for j in range((BLOCK // 2)):
                z, = struct.unpack("<H", ind[j * 2 : j * 2 + 2])
                if z == 0:
                    break
                chunk = read_block(f, z)
                need = size - len(out)
                out.extend(chunk[:need])
                if len(out) >= size:
                    break
        elif zones[zidx]:
            chunk = read_block(f, zones[zidx])
            need = size - len(out)
            out.extend(chunk[:need])
    return bytes(out[:size])


def type_name(mode):
    if (mode & IFMT) == IFDIR:
        return "directory"
    if (mode & IFMT) == IFREG:
        return "regular"
    return f"other(0x{mode & IFMT:o})"


def verify_entry(f, sb, path):
    inode, err = resolve_path(f, sb, path)
    if err:
        print(f"ROOTFS_VERIFY_FAIL path={path} error={err}")
        return False
    kind = type_name(inode["mode"])
    print(
        f"ROOTFS_STAT path={path} inode_mode=0x{inode['mode']:04x} "
        f"type={kind} size={inode['size']}"
    )
    return inode, kind


def main():
    if len(sys.argv) < 2:
        print(
            f"Usage: {sys.argv[0]} DISK_IMAGE [PATH ...]",
            file=sys.stderr,
        )
        sys.exit(1)

    disk_path = sys.argv[1]
    paths = sys.argv[2:] if len(sys.argv) > 2 else ["/bin", "/bin/busybox"]

    with open(disk_path, "rb") as f:
        sb = parse_super(read_block(f, 1))

        ok = True
        busybox_inode = None

        for path in paths:
            result = verify_entry(f, sb, path)
            if result is False:
                ok = False
                continue
            inode, kind = result

            if path == "/bin":
                if kind != "directory":
                    print(f"ROOTFS_BUSYBOX_WRONG_TYPE /bin must be directory, got {kind}")
                    ok = False
            if path == "/bin/busybox":
                busybox_inode = inode
                if kind != "regular":
                    print(
                        f"ROOTFS_BUSYBOX_WRONG_TYPE /bin/busybox must be regular file, "
                        f"got {kind} mode=0x{inode['mode']:04x} size={inode['size']}"
                    )
                    ok = False
                elif inode["size"] <= MIN_BUSYBOX_SIZE:
                    print(
                        f"ROOTFS_BUSYBOX_WRONG_TYPE /bin/busybox size too small "
                        f"({inode['size']} <= {MIN_BUSYBOX_SIZE})"
                    )
                    ok = False
                else:
                    prefix = read_file_prefix(f, sb, inode, 4)
                    if prefix != ELF_MAGIC:
                        print(
                            f"ROOTFS_BUSYBOX_WRONG_TYPE /bin/busybox missing ELF magic "
                            f"(got {prefix!r})"
                        )
                        ok = False
                    else:
                        print(
                            f"ROOTFS_BUSYBOX_FIXED_REGULAR_FILE size={inode['size']} "
                            f"elf_magic=OK"
                        )

        if not ok:
            sys.exit(1)

        if busybox_inode is not None:
            print("ROOTFS_VERIFY_OK busybox present as regular ELF file")
        else:
            print("ROOTFS_VERIFY_OK")


if __name__ == "__main__":
    main()
