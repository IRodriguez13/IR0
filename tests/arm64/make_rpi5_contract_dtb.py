#!/usr/bin/env python3
"""Build a minimal BCM2712-shaped DTB backed by QEMU virt devices."""
import struct
import sys

BE = lambda value: struct.pack(">I", value)
strings = bytearray()
names = {}

def nameoff(name):
    if name not in names:
        names[name] = len(strings)
        strings.extend(name.encode() + b"\0")
    return names[name]

blob = bytearray()
def align():
    while len(blob) & 3:
        blob.append(0)
def begin(name):
    blob.extend(BE(1)); blob.extend(name.encode() + b"\0"); align()
def end(): blob.extend(BE(2))
def prop(name, value):
    blob.extend(BE(3)); blob.extend(BE(len(value))); blob.extend(BE(nameoff(name)))
    blob.extend(value); align()
def cells(*values): return b"".join(BE(v) for v in values)
def text(value): return value.encode() + b"\0"

begin("")
prop("#address-cells", cells(2)); prop("#size-cells", cells(2))
prop("compatible", text("raspberrypi,5-model-b") + text("brcm,bcm2712"))
begin("memory@40000000")
prop("device_type", text("memory")); prop("reg", cells(0, 0x40000000, 0, 0x08000000)); end()
begin("cpus")
prop("#address-cells", cells(1)); prop("#size-cells", cells(0))
for cpu in range(4):
    begin(f"cpu@{cpu}"); prop("device_type", text("cpu")); prop("reg", cells(cpu)); end()
end()
begin("reserved-memory")
prop("#address-cells", cells(2)); prop("#size-cells", cells(2)); prop("ranges", b"")
begin("firmware@41000000"); prop("reg", cells(0, 0x41000000, 0, 0x10000)); end()
end()
begin("psci"); prop("compatible", text("arm,psci-1.0")); prop("method", text("hvc")); end()
begin("timer"); prop("compatible", text("arm,armv8-timer")); end()
begin("soc")
prop("#address-cells", cells(2)); prop("#size-cells", cells(2))
prop("ranges", cells(0, 0, 0, 0, 0, 0x40000000))
begin("interrupt-controller@8000000")
prop("compatible", text("arm,gic-400")); prop("reg", cells(0, 0x08000000, 0, 0x10000,
                                                           0, 0x08010000, 0, 0x10000)); end()
begin("serial@9000000")
prop("compatible", text("arm,pl011") + text("arm,primecell"));
prop("reg", cells(0, 0x09000000, 0, 0x1000)); prop("status", text("okay")); end()
end(); end(); blob.extend(BE(9))

reserve = b"\0" * 16
header_size = 40
reserve_off = header_size
struct_off = reserve_off + len(reserve)
strings_off = struct_off + len(blob)
total = strings_off + len(strings)
header = b"".join(BE(v) for v in (0xD00DFEED, total, struct_off, strings_off,
                                   reserve_off, 17, 16, 0, len(strings), len(blob)))
with open(sys.argv[1], "wb") as output:
    output.write(header + reserve + blob + strings)
