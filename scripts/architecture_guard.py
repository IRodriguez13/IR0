#!/usr/bin/env python3
"""
IR0 architecture guardrails.

Checks:
1) No direct <drivers/...> includes inside fs/* and kernel/syscalls.c.
2) Required facade headers for subsystem decoupling exist.
3) Portable trees (fs/kernel/mm/net) must not include interrupt controller headers
   (#include <interrupt/arch/...>);
4) Files under fs/ must not use #include <arch/...>; use includes/ir0/* facades instead.
"""

from pathlib import Path
import sys
import re


ROOT = Path(__file__).resolve().parent.parent

FORBIDDEN_PATHS = [
    ROOT / "fs",
    ROOT / "kernel" / "syscalls.c",
]

REQUIRED_FACADES = [
    ROOT / "includes" / "ir0" / "driver.h",
    ROOT / "includes" / "ir0" / "driver_bootstrap.h",
    ROOT / "includes" / "ir0" / "block_dev.h",
    ROOT / "includes" / "ir0" / "partition.h",
    ROOT / "includes" / "ir0" / "arch_port.h",
    ROOT / "includes" / "ir0" / "clock.h",
    ROOT / "includes" / "ir0" / "rtc.h",
    ROOT / "includes" / "ir0" / "serial_io.h",
    ROOT / "includes" / "ir0" / "audio_backend.h",
    ROOT / "includes" / "ir0" / "console_backend.h",
    ROOT / "includes" / "ir0" / "video_backend.h",
    ROOT / "includes" / "ir0" / "input_backend.h",
    ROOT / "includes" / "ir0" / "net.h",
    ROOT / "includes" / "ir0" / "bluetooth.h",
]

REQUIRED_ARM64_SCAFFOLD = [
    ROOT / "arch" / "arm64" / "linker.ld",
    ROOT / "arch" / "arm64" / "sources" / "boot_stub.c",
    ROOT / "arch" / "arm64" / "sources" / "arch_early.c",
    ROOT / "arch" / "arm64" / "sources" / "interrupts.c",
    ROOT / "arch" / "arm64" / "sources" / "syscall_stub.c",
]

DRIVER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]drivers/')

# interrupt/* is for hardware backends; portable code uses arch_portable/facades.
INTERRUPT_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]interrupt/arch/')

# fs/ pseudo-VFS layers must route CPU/HW probes through ir0/*.h wrappers.
FS_DIRECT_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')

PORTABLE_DIRS_NO_INTERRUPT_ARCH = [
    ROOT / "fs",
    ROOT / "kernel",
    ROOT / "mm",
    ROOT / "net",
]


def iter_c_files(base: Path):
    if base.is_file():
        yield base
        return
    for p in base.rglob("*"):
        if p.suffix in (".c", ".h"):
            yield p


def check_forbidden_includes():
    errors = []
    for target in FORBIDDEN_PATHS:
        for fpath in iter_c_files(target):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if DRIVER_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(f"[forbidden-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_facades():
    errors = []
    for facade in REQUIRED_FACADES:
        if not facade.exists():
            rel = facade.relative_to(ROOT)
            errors.append(f"[missing-facade] {rel}")
    return errors


def check_arm64_scaffold():
    errors = []
    for p in REQUIRED_ARM64_SCAFFOLD:
        if not p.exists():
            rel = p.relative_to(ROOT)
            errors.append(f"[missing-arm64-scaffold] {rel}")
    return errors


def check_interrupt_arch_portable():
    errors = []
    for base in PORTABLE_DIRS_NO_INTERRUPT_ARCH:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if INTERRUPT_ARCH_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[portable-no-interrupt-arch] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_fs_no_direct_arch():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_DIRECT_ARCH_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[fs-no-direct-arch] {rel}:{idx}: {line.strip()}")
    return errors


def main():
    errors = []
    errors.extend(check_forbidden_includes())
    errors.extend(check_facades())
    errors.extend(check_arm64_scaffold())
    errors.extend(check_interrupt_arch_portable())
    errors.extend(check_fs_no_direct_arch())

    if errors:
        print("[arch-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[arch-guard] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
