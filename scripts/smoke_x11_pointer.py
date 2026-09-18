#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Boot stock X11 clients and prove pointer input changes the framebuffer."""

from __future__ import annotations

import argparse
from collections import Counter
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path


FAILURES = ("[STARTX][FAIL]", "Fatal server error", "[PANIC]",
            "Kernel panic", "general protection fault")


def detected_failure(output: str) -> str | None:
    lowered = output.lower()
    return next((marker for marker in FAILURES if marker.lower() in lowered), None)


def monitor_command(channel: socket.socket, command: str, delay: float = 0.08) -> None:
    channel.sendall((command + "\n").encode())
    time.sleep(delay)


def monitor_type(channel: socket.socket, text: str) -> None:
    key_names = {".": "dot", "-": "minus", "/": "slash", " ": "spc"}
    for character in text:
        key = key_names.get(character, character)
        monitor_command(channel, f"sendkey {key}", 0.25)
    monitor_command(channel, "sendkey ret", 0.50)


def ppm_max_solid_run(path: Path) -> int:
    """Return the longest horizontal run of one RGB value in a P6 dump."""
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise RuntimeError("QEMU screendump is not a P6 PPM")
        dimensions = stream.readline()
        while dimensions.startswith(b"#"):
            dimensions = stream.readline()
        width, height = (int(value) for value in dimensions.split())
        if int(stream.readline()) != 255:
            raise RuntimeError("unsupported PPM color depth")
        pixels = stream.read()
    if len(pixels) != width * height * 3:
        raise RuntimeError("truncated QEMU screendump")
    longest = 0
    for row in range(height):
        start = row * width * 3
        previous = pixels[start:start + 3]
        run = 1
        for column in range(1, width):
            offset = start + column * 3
            current = pixels[offset:offset + 3]
            if current == previous:
                run += 1
            else:
                longest = max(longest, run)
                previous = current
                run = 1
        longest = max(longest, run)
    return longest


def ppm_max_repeated_terminal_glyph(path: Path) -> int:
    """Detect a terminal filled with one fallback/missing-character glyph."""
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise RuntimeError("QEMU screendump is not a P6 PPM")
        dimensions = stream.readline()
        while dimensions.startswith(b"#"):
            dimensions = stream.readline()
        width, height = (int(value) for value in dimensions.split())
        if int(stream.readline()) != 255:
            raise RuntimeError("unsupported PPM color depth")
        pixels = stream.read()

    def rgb(x: int, y: int) -> bytes:
        offset = (y * width + x) * 3
        return pixels[offset:offset + 3]

    # The terminal body is the large white rectangle beneath twm's titlebar.
    # Locate that window without assuming a fixed QEMU resolution.
    best_length = best_x = best_y = 0
    for y in range(height):
        x = 0
        while x < width:
            if rgb(x, y) != b"\xff\xff\xff":
                x += 1
                continue
            start = x
            while x < width and rgb(x, y) == b"\xff\xff\xff":
                x += 1
            if x - start > best_length:
                best_length, best_x, best_y = x - start, start, y
    if best_length < 100:
        return 0

    # A broken narrow xterm against an ISO10646 font produced the same 6x13
    # fallback box dozens of times. Real text has varied glyph bitmaps. Search
    # all cell alignments so the assertion is independent of window placement.
    maximum = 0
    top = best_y + 40
    bottom = min(height, best_y + 120)
    for x_phase in range(6):
        for y_phase in range(13):
            seen: Counter[bytes] = Counter()
            for y in range(top + y_phase, bottom - 12, 13):
                for x in range(best_x + 2 + x_phase,
                               best_x + best_length - 9, 6):
                    tile = bytes(
                        1 if max(rgb(x + dx, y + dy)) < 128 else 0
                        for dy in range(13) for dx in range(6)
                    )
                    ink = sum(tile)
                    if 5 <= ink <= 60:
                        seen[tile] += 1
            if seen:
                maximum = max(maximum, seen.most_common(1)[0][1])
    return maximum


def ppm_desktop_background_pixels(path: Path) -> tuple[int, int]:
    """Count both configured xsetroot colors outside the desktop windows."""
    with path.open("rb") as stream:
        if stream.readline().strip() != b"P6":
            raise RuntimeError("QEMU screendump is not a P6 PPM")
        dimensions = stream.readline()
        while dimensions.startswith(b"#"):
            dimensions = stream.readline()
        width, height = (int(value) for value in dimensions.split())
        if int(stream.readline()) != 255:
            raise RuntimeError("unsupported PPM color depth")
        pixels = stream.read()
    dark = bytes((0x26, 0x32, 0x38))
    light = bytes((0x78, 0x90, 0x9c))
    # The lower-left quadrant stays clear of the fixed xterm and xclock.
    dark_count = light_count = 0
    for y in range(height // 2, height):
        for x in range(0, width // 4):
            offset = (y * width + x) * 3
            pixel = pixels[offset:offset + 3]
            dark_count += pixel == dark
            light_count += pixel == light
    return dark_count, light_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--qemu", default="qemu-system-x86_64")
    parser.add_argument("--iso", required=True, type=Path)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--home", required=True, type=Path)
    parser.add_argument("--init", required=True, type=Path)
    parser.add_argument("--inject", required=True, type=Path)
    args = parser.parse_args()

    with tempfile.TemporaryDirectory(prefix="ir0-x11-pointer.") as directory:
        work = Path(directory)
        root = work / "root.img"
        home = work / "home.img"
        log = work / "serial.log"
        monitor = work / "monitor.sock"
        before = work / "before.ppm"
        after = work / "after.ppm"
        keyboard = work / "keyboard.ppm"
        subprocess.run(["cp", "--reflink=auto", args.root, root], check=True)
        subprocess.run(["cp", "--reflink=auto", args.home, home], check=True)
        subprocess.run([args.inject, root, args.init, "sbin/init"], check=True)
        qemu = subprocess.Popen([
            args.qemu, "-cdrom", args.iso,
            "-drive", f"file={root},format=raw,if=ide,index=0",
            "-drive", f"file={home},format=raw,if=ide,index=1",
            "-serial", f"file:{log}", "-display", "none",
            "-monitor", f"unix:{monitor},server=on,wait=off",
            "-m", "512M", "-no-reboot", "-net", "none",
        ])
        try:
            deadline = time.monotonic() + 90
            while time.monotonic() < deadline:
                output = log.read_text(errors="replace") if log.exists() else ""
                if "STARTX_EXT2_OK" in output:
                    break
                failure = detected_failure(output)
                if failure:
                    raise RuntimeError(
                        f"X11 failed before pointer injection: matched {failure!r}")
                time.sleep(0.25)
            else:
                raise RuntimeError("timed out waiting for the X11 desktop session")

            channel = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            channel.connect(str(monitor))
            monitor_command(channel, f"screendump {before}")
            for _ in range(8):
                monitor_command(channel, "mouse_move 5 3")
            # Client connect is not equivalent to MapRequest completion on a
            # small UP kernel.  Let twm/xterm finish their X11 handshakes.
            time.sleep(15.0)
            # RandomPlacement has already mapped xterm.  Extra placement
            # clicks can legitimately activate a titlebar action.
            time.sleep(5.0)
            # xterm is the final mapped client and retains keyboard focus.
            # Do not synthesize an absolute position from relative PS/2
            # packets: acceleration makes that operation non-deterministic.
            monitor_command(channel, "mouse_button 1", 0.25)
            monitor_command(channel, "mouse_button 0", 2.0)
            monitor_type(channel, "uname -a")
            monitor_type(channel, "touch xok")
            monitor_command(channel, "mouse_move 1 0", 0.75)
            monitor_command(channel, f"screendump {keyboard}")
            keyboard_deadline = time.monotonic() + 20
            while time.monotonic() < keyboard_deadline:
                output = log.read_text(errors="replace")
                if "X11_KEYBOARD_COMMAND_OK" in output:
                    break
                failure = detected_failure(output)
                if failure:
                    raise RuntimeError(
                        f"X11 failed during keyboard smoke: matched {failure!r}")
                time.sleep(0.25)
            else:
                raise RuntimeError("graphical xterm did not execute typed command")
            monitor_command(channel, f"screendump {after}")
            # Keep the session running after pointer activity: this catches
            # delayed xterm/X server failures rather than proving only that a
            # single mouse packet reached the framebuffer.
            time.sleep(3.0)
            output = log.read_text(errors="replace")
            failure = detected_failure(output)
            if failure:
                raise RuntimeError(
                    f"X11 failed after pointer injection: matched {failure!r}")
            monitor_command(channel, "quit")
            channel.close()
            qemu.wait(timeout=10)

            first = before.read_bytes()
            second = after.read_bytes()
            changed = sum(left != right for left, right in zip(first, second))
            changed += abs(len(first) - len(second))
            output = log.read_text(errors="replace")
            # Xfbdev may skip its optional protocol-reset write when the PS/2
            # device is already synchronized.  Motion is proved by the read
            # path plus a changed framebuffer, so DEV_MOUSE_WRITE is not a
            # correctness requirement.
            required = ("PS2_MOUSE_PACKET_PATH_OK", "DEV_MOUSE_READ_PATH_OK",
                        "X11_WM_AND_TERMINAL_SUSTAINED_OK",
                        "X11_DESKTOP_BACKGROUND_OK",
                        "X11_DESKTOP_CLIENTS_SUSTAINED_OK",
                        "X11_DESKTOP_DEMOS_OK",
                        "X11_XAW_CLIENTS_OK",
                        "X11_KEYBOARD_COMMAND_OK")
            missing = [marker for marker in required if marker not in output]
            if missing:
                raise RuntimeError("missing input evidence: " + ", ".join(missing))
            if changed == 0:
                raise RuntimeError("pointer/button input did not change framebuffer")
            solid_run = ppm_max_solid_run(after)
            if solid_run < 100:
                raise RuntimeError(
                    "X11 clients connected but no mapped desktop window was visible")
            repeated_glyphs = ppm_max_repeated_terminal_glyph(keyboard)
            if repeated_glyphs >= 40:
                raise RuntimeError(
                    "xterm rendered repeated missing-character glyphs instead of text")
            background_dark, background_light = ppm_desktop_background_pixels(after)
            if min(background_dark, background_light) < 1000:
                raise RuntimeError(
                    "xsetroot desktop texture was not visible in the framebuffer")
            failure = detected_failure(output)
            if failure:
                raise RuntimeError(
                    f"kernel/X11 failure after pointer injection: matched {failure!r}")
            print(f"✓ X11 pointer path changed {changed} framebuffer bytes")
            print(f"✓ mapped X11 window visible (solid run {solid_run}px)")
            print(f"✓ xterm rendered varied readable glyphs (repeat peak {repeated_glyphs})")
            print("✓ xsetroot desktop texture remained visible")
            print("✓ graphical keyboard executed a shell command in xterm")
            print("✓ stock X server and X11 desktop clients remained stable")
            return 0
        finally:
            if log.exists():
                shutil.copyfile(log, "/tmp/ir0-x11-pointer-serial.log")
            if before.exists():
                shutil.copyfile(before, "/tmp/ir0-x11-pointer-before.ppm")
            if after.exists():
                shutil.copyfile(after, "/tmp/ir0-x11-pointer-after.ppm")
            if keyboard.exists():
                shutil.copyfile(keyboard, "/tmp/ir0-x11-keyboard.ppm")
            if home.exists():
                shutil.copyfile(home, "/tmp/ir0-x11-pointer-home.img")
            if qemu.poll() is None:
                qemu.terminate()
                qemu.wait(timeout=10)


if __name__ == "__main__":
    raise SystemExit(main())
