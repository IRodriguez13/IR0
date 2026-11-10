#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Append a root-level simple-framebuffer node to a flattened devicetree.

Usage: add_simplefb_dtb.py IN.dtb OUT.dtb
The framebuffer (800x600 a8r8g8b8 at 0x4f000000) sits inside the first
256 MiB of QEMU virt RAM, as a firmware carve-out does on Raspberry Pi.
"""
import struct, sys
src, dst = sys.argv[1], sys.argv[2]
base, size, w, h = 0x4F000000, 0x1D4C00, 800, 600
d = bytearray(open(src, "rb").read())
(magic, totalsize, off_struct, off_strings, off_rsv, version, lcv, bootcpu,
 size_strings, size_struct) = struct.unpack(">10I", d[:40])
assert magic == 0xD00DFEED
structb = bytearray(d[off_struct:off_struct + size_struct])
strings = bytearray(d[off_strings:off_strings + size_strings])
rsv = d[off_rsv:off_struct]
def stroff(name):
    key = name.encode() + b"\0"
    i = strings.find(key)
    if i >= 0 and (i == 0 or strings[i - 1] == 0):
        return i
    strings.extend(key)
    return len(strings) - len(key)
def pad(b):
    while len(b) & 3:
        b.append(0)
node = bytearray(struct.pack(">I", 1) + b"framebuffer@4f000000\0"); pad(node)
def prop(name, val):
    node.extend(struct.pack(">III", 3, len(val), stroff(name))); node.extend(val); pad(node)
prop("compatible", b"simple-framebuffer\0")
prop("reg", struct.pack(">IIII", 0, base, 0, size))
prop("width", struct.pack(">I", w)); prop("height", struct.pack(">I", h))
prop("stride", struct.pack(">I", w * 4)); prop("format", b"a8r8g8b8\0")
node.extend(struct.pack(">I", 2))
# root END_NODE is the token right before FDT_END (last 8 bytes).
assert structb[-4:] == struct.pack(">I", 9) and structb[-8:-4] == struct.pack(">I", 2)
structb = structb[:-8] + node + structb[-8:]
hdr_len = 40
off_rsv2 = (hdr_len + 7) & ~7
out = bytearray(off_rsv2)
out.extend(rsv)
off_struct2 = len(out); out.extend(structb)
off_strings2 = len(out); out.extend(strings)
struct.pack_into(">10I", out, 0, magic, len(out), off_struct2, off_strings2, off_rsv2,
                 version, lcv, bootcpu, len(strings), len(structb))
open(dst, "wb").write(out)
