#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Inject a file into a MINIX v1 disk image without mounting (no minix module).

Usage:
  inject_init_minix.py --format DISK_IMAGE
  inject_init_minix.py --format-large DISK_IMAGE
  inject_init_minix.py [--setuid|--mode OCTAL] [--owner UID:GID] \
      DISK_IMAGE FILE [DEST_PATH]
  inject_init_minix.py --hardlink DISK_IMAGE EXISTING_DEST NEW_DEST
  inject_init_minix.py --owner UID:GID [--mode OCTAL] --chown DISK_IMAGE PATH

  DEST_PATH: slash-separated path without leading slash (default: sbin/init)
  Examples:
    inject_init_minix.py --format disk.img
    inject_init_minix.py disk.img setup/pid1/init
    inject_init_minix.py disk.img setup/pid1/sh_smoke bin/sh
    inject_init_minix.py --hardlink disk.img bin/busybox bin/ls
"""

import os
import struct
import sys
import time

BLOCK = 1024
INODE_SIZE = 32
NAME_LEN = 14
DIR_ENTRY = 16
MAGIC = 0x137F
IFDIR = 0o040000
IFREG = 0o100000
IFMT = 0o170000


def now_mtime(path=None):
    """Unix time for MINIX v1 inode mtime (avoid epoch 1970 on ls -l)."""
    if path:
        try:
            return int(os.stat(path).st_mtime) & 0xFFFFFFFF
        except OSError:
            pass
    return int(time.time()) & 0xFFFFFFFF


def inject_verbose():
    """Per-file inject audit (MINIX_INJECT / Injected). Off by default."""
    return os.environ.get("IR0_INJECT_VERBOSE", "").strip().lower() in (
        "1",
        "true",
        "yes",
    )


def vprint(*args, **kwargs):
    if inject_verbose():
        print(*args, **kwargs)


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


def inode_type(mode):
    return mode & IFMT


def collect_zones_from_inode(f, inode):
    """
    Return every zone owned by an inode: data + single/double-indirect
    pointer blocks. sync_zmap must mark metadata zones allocated; omitting
    them frees indirect blocks and the next inject overwrites them (init SEGV).
    """
    zones = set()
    if (inode["mode"] & IFMT) != IFDIR and (inode["mode"] & IFMT) != IFREG:
        return zones
    if inode["zones"][0] == 0 and (inode["mode"] & IFMT) == IFDIR:
        return zones
    for zidx in range(7):
        z = inode["zones"][zidx]
        if z:
            zones.add(z)
    if inode["zones"][7]:
        ind_z = inode["zones"][7]
        zones.add(ind_z)
        ind = read_block(f, ind_z)
        for j in range(BLOCK // 2):
            z, = struct.unpack("<H", ind[j * 2 : j * 2 + 2])
            if z == 0:
                break
            zones.add(z)
    if inode["zones"][8]:
        dind_z = inode["zones"][8]
        zones.add(dind_z)
        dind = read_block(f, dind_z)
        for j in range(BLOCK // 2):
            z1, = struct.unpack("<H", dind[j * 2 : j * 2 + 2])
            if z1 == 0:
                break
            zones.add(z1)
            lvl1 = read_block(f, z1)
            for k in range(BLOCK // 2):
                z2, = struct.unpack("<H", lvl1[k * 2 : k * 2 + 2])
                if z2 == 0:
                    break
                zones.add(z2)
    return zones


def collect_used_inodes(f, sb):
    """Walk the dentry tree and scan the inode table for allocated inodes."""
    used = set()
    queue = [1]
    used.add(1)
    while queue:
        num = queue.pop()
        inode = read_inode(f, sb, num)
        for child_ino, _name in dir_entries(f, inode):
            if child_ino == 0 or child_ino > sb["ninodes"]:
                continue
            if child_ino not in used:
                used.add(child_ino)
                queue.append(child_ino)
    for n in range(1, sb["ninodes"] + 1):
        if n in used:
            continue
        inode = read_inode(f, sb, n)
        if inode["mode"] != 0 and inode["nlinks"] > 0:
            used.add(n)
    return used


def sync_imap_from_tree(f, sb):
    """Rebuild imap from the live tree (+ orphan inodes with mode/nlinks)."""
    used = collect_used_inodes(f, sb)
    imap = bytearray(BLOCK)  # bit set = allocated; start empty then mark used
    for n in used:
        byte_i = n // 8
        bit_i = n % 8
        if byte_i < BLOCK:
            imap[byte_i] |= 1 << bit_i
    write_block(f, imap_block(sb), bytes(imap))
    return used


def sync_zmap_from_inodes(f, sb, used_inodes=None):
    """Rebuild zmap from inodes (bit set = free, clear = allocated)."""
    if used_inodes is None:
        used_inodes = collect_used_inodes(f, sb)
    used_zones = set()
    for n in used_inodes:
        inode = read_inode(f, sb, n)
        used_zones.update(collect_zones_from_inode(f, inode))
    zmap_blk_base = zmap_start(sb)
    for blk in range(sb["zmap_blocks"]):
        write_block(f, zmap_blk_base + blk, bytes(b"\xff" * BLOCK))
    for zone in used_zones:
        if zone < sb["firstdatazone"] or zone >= sb["nzones"]:
            continue
        idx = zone - sb["firstdatazone"]
        byte_i = idx // 8
        bit_i = idx % 8
        zmap_blk = zmap_blk_base + byte_i // BLOCK
        zoff = byte_i % BLOCK
        zmap = bytearray(read_block(f, zmap_blk))
        zmap[zoff] &= ~(1 << bit_i)
        write_block(f, zmap_blk, bytes(zmap))


def sync_bitmaps_from_tree(f, sb):
    used = sync_imap_from_tree(f, sb)
    sync_zmap_from_inodes(f, sb, used)
    return used


# Sequential hint: avoid rescanning the whole zmap from firstdatazone each time.
_zone_alloc_hint = 0


def alloc_inode(f, sb):
    """Allocate from imap; caller must have a coherent map (format or sync)."""
    imap = bytearray(read_block(f, imap_block(sb)))
    limit = min(sb["ninodes"] + 1, BLOCK * 8)
    for n in range(1, limit):
        byte_i = n // 8
        bit_i = n % 8
        if (imap[byte_i] & (1 << bit_i)) == 0:
            imap[byte_i] |= 1 << bit_i
            write_block(f, imap_block(sb), bytes(imap))
            return n
    raise SystemExit("no free inode")


def zone_free(f, sb, zone):
    if zone < sb["firstdatazone"] or zone >= sb["nzones"]:
        return False
    idx = zone - sb["firstdatazone"]
    byte_i = idx // 8
    bit_i = idx % 8
    zmap_blk = zmap_start(sb) + byte_i // BLOCK
    zoff = byte_i % BLOCK
    zmap = read_block(f, zmap_blk)
    return (zmap[zoff] & (1 << bit_i)) != 0


def alloc_zone(f, sb):
    """
    Allocate one data zone from the on-disk zmap.

    Do NOT rebuild zmap here: pack-minix calls this per block of every file.
    With nzones=65535, sync-per-alloc made desktop inject appear hung.
    """
    global _zone_alloc_hint
    zmap_blk_base = zmap_start(sb)
    first = sb["firstdatazone"]
    nzones = sb["nzones"]
    hint = _zone_alloc_hint if _zone_alloc_hint >= first else first

    def try_from(start_zone):
        idx0 = start_zone - first
        start_blk = idx0 // (BLOCK * 8)
        for zmap_i in range(start_blk, sb["zmap_blocks"]):
            zmap = bytearray(read_block(f, zmap_blk_base + zmap_i))
            bit_base = zmap_i * BLOCK * 8
            off0 = 0 if zmap_i > start_blk else (idx0 % (BLOCK * 8)) // 8
            for zoff in range(off0, BLOCK):
                byte_val = zmap[zoff]
                if byte_val == 0:
                    continue
                bit0 = 0 if not (zmap_i == start_blk and zoff == off0) else (idx0 % 8)
                for bit_i in range(bit0, 8):
                    if (byte_val & (1 << bit_i)) == 0:
                        continue
                    zone = first + bit_base + zoff * 8 + bit_i
                    if zone < first or zone >= nzones:
                        continue
                    zmap[zoff] &= ~(1 << bit_i)
                    write_block(f, zmap_blk_base + zmap_i, bytes(zmap))
                    return zone
        return None

    zone = try_from(hint)
    if zone is None and hint > first:
        zone = try_from(first)
    if zone is None:
        raise SystemExit("no free zone")
    _zone_alloc_hint = zone + 1
    return zone


def dir_zone_list(inode):
    """Direct directory zones (MINIX v1 i_zone[0..6]); matches fs/minix_fs.c."""
    return [z for z in inode["zones"][:7] if z]


def dir_entries(f, inode):
    if (inode["mode"] & IFMT) != IFDIR:
        return []
    out = []
    for z in dir_zone_list(inode):
        raw = read_block(f, z)
        for i in range(0, BLOCK, DIR_ENTRY):
            ino = struct.unpack("<H", raw[i : i + 2])[0]
            name = raw[i + 2 : i + DIR_ENTRY]
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
    nb = name.encode("ascii")[:NAME_LEN]
    for z in dir_zone_list(dir_inode):
        raw = bytearray(read_block(f, z))
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
    vprint(
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

    # Wipe inode table. Re-format of a previously packed image must not leave
    # stale mode!=0 inodes: sync_imap_from_tree would mark them used and the
    # next pack runs out of free inodes / corrupts root dentries (missing /etc).
    itab = 2 + imap_blocks + zmap_blocks
    for blk in range(inode_table_blocks):
        write_block(f, itab + blk, bytes(BLOCK))

    root_zone = sb["firstdatazone"]
    root_inode = {
        "mode": IFDIR | 0o755,
        "uid": 0,
        "size": 2 * DIR_ENTRY,
        "mtime": now_mtime(),
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

    vprint(
        f"MINIX_FORMAT ok magic=0x{MAGIC:04x} ninodes={sb['ninodes']} "
        f"nzones={sb['nzones']} firstdatazone={sb['firstdatazone']}"
    )
    global _zone_alloc_hint
    _zone_alloc_hint = sb["firstdatazone"]
    return sb


def add_dir_entry(f, sb, dir_inode, dir_num, name, child_num):
    nb = name.encode("ascii")[:NAME_LEN]

    def write_slot(zone, raw, off):
        raw[off : off + 2] = struct.pack("<H", child_num)
        raw[off + 2 : off + DIR_ENTRY] = b"\x00" * (DIR_ENTRY - 2)
        raw[off + 2 : off + 2 + len(nb)] = nb
        write_block(f, zone, bytes(raw))
        dir_inode["size"] += DIR_ENTRY
        write_inode(f, sb, dir_num, dir_inode)

    for zi in range(7):
        z = dir_inode["zones"][zi]
        if z == 0:
            continue
        raw = bytearray(read_block(f, z))
        for i in range(0, BLOCK, DIR_ENTRY):
            ino = struct.unpack("<H", raw[i : i + 2])[0]
            if ino != 0:
                continue
            write_slot(z, raw, i)
            return

    # Grow directory with another direct zone (kernel walks i_zone[0..6]).
    for zi in range(7):
        if dir_inode["zones"][zi] != 0:
            continue
        z = alloc_zone(f, sb)
        dir_inode["zones"][zi] = z
        raw = bytearray(BLOCK)
        write_slot(z, raw, 0)
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
        "mtime": now_mtime(),
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


def prepare_regular_file(f, sb, file_inode, data, file_mode=0o755):
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

    file_inode["mode"] = IFREG | file_mode
    file_inode["size"] = len(data)
    file_inode["zones"] = zones
    file_inode["nlinks"] = 1
    return file_inode


def chown_path(f, sb, path_parts, uid, gid, file_mode=None):
    """Set owner (and optionally mode) of an existing file or directory."""
    ino, inode = resolve_path_inode(f, sb, path_parts)
    if ino == 0 or inode is None:
        raise SystemExit("chown target missing: /" + "/".join(path_parts))
    inode["uid"] = uid
    inode["gid"] = gid
    if file_mode is not None:
        inode["mode"] = (inode["mode"] & IFMT) | file_mode
    write_inode(f, sb, ino, inode)
    audit_entry(
        "(chown)",
        "/" + "/".join(path_parts),
        "directory" if (inode["mode"] & IFMT) == IFDIR else "file",
        inode["mode"],
        ino,
        inode["size"],
    )


def write_file(f, sb, path_parts, data, source_path, file_mode=0o755,
               owner=None):
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
                elif existing.get("nlinks", 1) > 1:
                    # Hardlink: do not rewrite the shared inode (that would
                    # clobber BusyBox when replacing halt/poweroff wrappers).
                    # Detach this name and allocate a fresh inode.
                    remove_dir_entry(f, sb, cur, cur_num, part)
                    existing["nlinks"] = max(1, existing["nlinks"] - 1)
                    write_inode(f, sb, ino, existing)
                    ino = 0
            if ino == 0:
                ino = alloc_inode(f, sb)
                new_entry = True
                file_inode = {
                    "mode": IFREG | file_mode,
                    "uid": 0,
                    "size": 0,
                    "mtime": now_mtime(source_path),
                    "gid": 0,
                    "nlinks": 1,
                    "zones": [0] * 9,
                }
            else:
                file_inode = read_inode(f, sb, ino)
                if (file_inode["mode"] & IFMT) != IFREG:
                    file_inode = {
                        "mode": IFREG | file_mode,
                        "uid": 0,
                        "size": 0,
                        "mtime": now_mtime(source_path),
                        "gid": 0,
                        "nlinks": 1,
                        "zones": [0] * 9,
                    }

            file_inode = prepare_regular_file(f, sb, file_inode, data, file_mode)
            file_inode["mtime"] = now_mtime(source_path)
            if owner is not None:
                file_inode["uid"], file_inode["gid"] = owner
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


def resolve_path_inode(f, sb, path_parts):
    """Return (inode_num, inode_dict) for an existing path, or (0, None)."""
    if not path_parts:
        return 1, read_inode(f, sb, 1)
    cur_num = 1
    cur = read_inode(f, sb, 1)
    for i, part in enumerate(path_parts):
        ino = find_in_dir(f, sb, cur, part)
        if ino == 0:
            return 0, None
        if i == len(path_parts) - 1:
            return ino, read_inode(f, sb, ino)
        cur_num = ino
        cur = read_inode(f, sb, ino)
    return 0, None


def hardlink_path(f, sb, existing_parts, new_parts):
    """Add NEW_PATH as another directory entry for EXISTING_PATH's inode."""
    src_ino, src = resolve_path_inode(f, sb, existing_parts)
    if src_ino == 0 or src is None:
        raise SystemExit(
            "hardlink source missing: /" + "/".join(existing_parts)
        )
    if (src["mode"] & IFMT) != IFREG:
        raise SystemExit(
            "hardlink source not a regular file: /" + "/".join(existing_parts)
        )

    cur_num = 1
    cur = read_inode(f, sb, 1)
    dest_prefix = ""
    for i, part in enumerate(new_parts):
        is_last = i == len(new_parts) - 1
        if is_last:
            existing = find_in_dir(f, sb, cur, part)
            if existing != 0:
                if existing == src_ino:
                    return src_ino
                # Replace stale applet name (e.g. BusyBox reinject): detach
                # the old directory entry so the hardlink can point at the
                # new multicall inode.
                old = read_inode(f, sb, existing)
                remove_dir_entry(f, sb, cur, cur_num, part)
                if old.get("nlinks", 1) > 1:
                    old["nlinks"] = max(1, old["nlinks"] - 1)
                    write_inode(f, sb, existing, old)
                existing = 0
            add_dir_entry(f, sb, cur, cur_num, part, src_ino)
            src["nlinks"] = min(255, src["nlinks"] + 1)
            write_inode(f, sb, src_ino, src)
            audit_entry(
                "/" + "/".join(existing_parts),
                "/" + "/".join(new_parts),
                "hardlink",
                src["mode"],
                src_ino,
                src["size"],
            )
            return src_ino

        dest_prefix = dest_prefix + "/" + part if dest_prefix else "/" + part
        ino = find_in_dir(f, sb, cur, part)
        if ino == 0:
            parent_audit = dest_prefix.rsplit("/", 1)[0] or "/"
            ino, cur = mkdir(f, sb, cur_num, cur, part, parent_audit)
            cur_num = ino
        else:
            cur_num = ino
            cur = read_inode(f, sb, ino)
    raise SystemExit("hardlink: empty NEW_DEST")


def parse_owner(spec):
    """UID:GID string → (uid, gid). MINIX v1 stores an 8-bit GID."""
    try:
        uid_s, gid_s = spec.split(":", 1)
        uid = int(uid_s, 10)
        gid = int(gid_s, 10)
    except ValueError:
        raise SystemExit("--owner needs UID:GID (decimal)")
    if not 0 <= uid <= 0xFFFF:
        raise SystemExit(f"uid {uid} does not fit MINIX v1 (16-bit)")
    if not 0 <= gid <= 0xFF:
        raise SystemExit(f"gid {gid} does not fit MINIX v1 (8-bit)")
    return uid, gid


def main():
    file_mode = 0o755
    mode_given = False
    owner = None
    argv = sys.argv[:]
    if "--setuid" in argv:
        file_mode = 0o4755
        mode_given = True
        argv.remove("--setuid")
    if "--mode" in argv:
        idx = argv.index("--mode")
        try:
            file_mode = int(argv[idx + 1], 8)
        except (IndexError, ValueError):
            print("--mode needs an octal permission value", file=sys.stderr)
            sys.exit(1)
        mode_given = True
        del argv[idx : idx + 2]
    if "--owner" in argv:
        idx = argv.index("--owner")
        if idx + 1 >= len(argv):
            print("--owner needs UID:GID", file=sys.stderr)
            sys.exit(1)
        owner = parse_owner(argv[idx + 1])
        del argv[idx : idx + 2]
    sys.argv = argv

    if len(sys.argv) >= 2 and sys.argv[1] == "--chown":
        if len(sys.argv) != 4 or owner is None:
            print(
                f"Usage: {sys.argv[0]} --owner UID:GID [--mode OCTAL] "
                "--chown DISK_IMAGE PATH",
                file=sys.stderr,
            )
            sys.exit(1)
        disk_path = sys.argv[2]
        path_parts = [p for p in sys.argv[3].split("/") if p]
        with open(disk_path, "r+b") as f:
            sb = parse_super(read_block(f, 1))
            sync_bitmaps_from_tree(f, sb)
            chown_path(
                f,
                sb,
                path_parts,
                owner[0],
                owner[1],
                file_mode if mode_given else None,
            )
            sync_bitmaps_from_tree(f, sb)
        vprint(
            f"chown {disk_path}:/{'/'.join(path_parts)} -> "
            f"{owner[0]}:{owner[1]}"
        )
        return

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
                # Headroom for TinyCC headers/libs + GNU make + BusyBox applets.
                # (Must wipe inode table — see format_minix_v1 — or re-pack fails.)
                format_minix_v1(f, ninodes=2048, nzones=65535)
            else:
                format_minix_v1(f)
        vprint(f"format {disk_path} MINIX v1")
        return

    if len(sys.argv) >= 2 and sys.argv[1] == "--hardlink":
        if len(sys.argv) != 5:
            print(
                f"Usage: {sys.argv[0]} --hardlink DISK_IMAGE EXISTING_DEST NEW_DEST",
                file=sys.stderr,
            )
            sys.exit(1)
        disk_path = sys.argv[2]
        existing_parts = [p for p in sys.argv[3].split("/") if p]
        new_parts = [p for p in sys.argv[4].split("/") if p]
        if not existing_parts or not new_parts:
            print("invalid hardlink paths", file=sys.stderr)
            sys.exit(1)
        with open(disk_path, "r+b") as f:
            sb = parse_super(read_block(f, 1))
            sync_bitmaps_from_tree(f, sb)
            hardlink_path(f, sb, existing_parts, new_parts)
            sync_bitmaps_from_tree(f, sb)
        dest_display = "/" + "/".join(new_parts)
        src_display = "/" + "/".join(existing_parts)
        vprint(f"hardlink {src_display} -> {disk_path}:{dest_display}")
        return

    if len(sys.argv) < 3 or len(sys.argv) > 5:
        print(
            f"Usage: {sys.argv[0]} --format DISK_IMAGE",
            file=sys.stderr,
        )
        print(
            f"       {sys.argv[0]} --format-large DISK_IMAGE",
            file=sys.stderr,
        )
        print(
            f"       {sys.argv[0]} [--setuid|--mode OCTAL] DISK_IMAGE FILE [DEST_PATH]",
            file=sys.stderr,
        )
        print(
            f"       {sys.argv[0]} --hardlink DISK_IMAGE EXISTING_DEST NEW_DEST",
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
        # One rebuild per inject process; alloc_* maintain bitmaps afterward.
        sync_bitmaps_from_tree(f, sb)
        write_file(f, sb, path_parts, data, file_path, file_mode, owner)

    dest_display = "/" + "/".join(path_parts)
    verify_script = os.path.join(os.path.dirname(__file__), "verify_minix_rootfs.py")
    import subprocess

    verify_paths = [dest_display]
    if dest_display.startswith("/sbin/") and dest_display != "/sbin":
        verify_paths.insert(0, "/sbin")
    rc = subprocess.run(
        [sys.executable, verify_script, disk_path, *verify_paths],
        check=False,
    )
    if rc.returncode != 0:
        raise SystemExit(f"post-inject verify failed for {dest_display}")
    vprint(
        f"inject {file_path} -> {disk_path}:{dest_display} "
        f"({len(data)} bytes)"
    )


if __name__ == "__main__":
    main()
