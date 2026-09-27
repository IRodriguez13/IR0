#!/usr/bin/env python3
"""Build the minimal Raspberry Pi 3 firmware contract used by QEMU raspi3b."""
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
    blob.extend(BE(1))
    blob.extend(name.encode() + b"\0")
    align()


def end():
    blob.extend(BE(2))


def prop(name, value):
    blob.extend(BE(3))
    blob.extend(BE(len(value)))
    blob.extend(BE(nameoff(name)))
    blob.extend(value)
    align()


def cells(*values):
    return b"".join(BE(value) for value in values)


def text(value):
    return value.encode() + b"\0"


begin("")
prop("#address-cells", cells(2))
prop("#size-cells", cells(2))
prop("compatible", text("raspberrypi,3-model-b") + text("brcm,bcm2837"))
begin("memory@0")
prop("device_type", text("memory"))
prop("reg", cells(0, 0, 0, 0x40000000))
end()
begin("cpus")
prop("#address-cells", cells(1))
prop("#size-cells", cells(0))
for cpu in range(4):
    begin(f"cpu@{cpu}")
    prop("device_type", text("cpu"))
    prop("reg", cells(cpu))
    end()
end()
begin("timer")
prop("compatible", text("arm,armv8-timer"))
end()
begin("local_intc@40000000")
prop("compatible", text("brcm,bcm2836-l1-intc"))
prop("reg", cells(0, 0x40000000, 0, 0x100))
end()
begin("serial@3f201000")
prop("compatible", text("arm,pl011") + text("arm,primecell"))
prop("reg", cells(0, 0x3F201000, 0, 0x1000))
prop("status", text("okay"))
end()
end()
blob.extend(BE(9))

reserve = b"\0" * 16
header_size = 40
reserve_off = header_size
struct_off = reserve_off + len(reserve)
strings_off = struct_off + len(blob)
total = strings_off + len(strings)
header = b"".join(
    BE(value)
    for value in (
        0xD00DFEED,
        total,
        struct_off,
        strings_off,
        reserve_off,
        17,
        16,
        0,
        len(strings),
        len(blob),
    )
)
with open(sys.argv[1], "wb") as output:
    output.write(header + reserve + blob + strings)
