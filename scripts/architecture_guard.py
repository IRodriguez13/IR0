#!/usr/bin/env python3
"""
IR0 architecture guardrails.

Checks:
1) No direct <drivers/...> includes inside fs/* and kernel/syscalls.c.
2) Required facade headers for subsystem decoupling exist.
3) Portable trees (fs/kernel/mm/net) must not include interrupt controller headers
   (#include <interrupt/arch/...>);
4) Files under fs/ must not use #include <arch/...>; use includes/ir0/* facades instead.
5) fs/, mm/, net/, drivers/: no #include <kernel/*.h> (no whitelist).
6) mm/, net/: no #include <arch/...>.
7) drivers/: no raw #include of drivers/storage/block_dev.h — use ir0/block_dev.h.
8) Paths outside drivers/bluetooth/ must not #include bluetooth/...
9) drivers/: no #include <arch/...> (use ir0/arch_port.h).
10) kernel/: no #include <drivers/...> (whole tree).
11) kernel/: no #include <arch/common/arch_portable.h> (use ir0/arch_port.h).
12) fs/: no #include <mm/...> (use ir0/mm_port.h or narrower facades).
13) debug_bins/: no #include "test/... except debug_bins/cmd_ktest.c (IR0_KERNEL_TESTS).
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
    ROOT / "includes" / "ir0" / "mm_port.h",
    ROOT / "includes" / "ir0" / "clock.h",
    ROOT / "includes" / "ir0" / "rtc.h",
    ROOT / "includes" / "ir0" / "serial_io.h",
    ROOT / "includes" / "ir0" / "audio_backend.h",
    ROOT / "includes" / "ir0" / "console_backend.h",
    ROOT / "includes" / "ir0" / "video_backend.h",
    ROOT / "includes" / "ir0" / "input_backend.h",
    ROOT / "includes" / "ir0" / "net.h",
    ROOT / "includes" / "ir0" / "bluetooth.h",
    ROOT / "includes" / "ir0" / "usb_host.h",
    ROOT / "includes" / "ir0" / "resource_registry.h",
    ROOT / "includes" / "ir0" / "init_drv.h",
    ROOT / "includes" / "ir0" / "scheduler_api.h",
    ROOT / "includes" / "ir0" / "pseudo_fs.h",
    ROOT / "includes" / "ir0" / "credentials.h",
    ROOT / "includes" / "ir0" / "ktm" / "ktm.h",
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

# mm/ and net/ must not pull arch headers directly — use ir0/arch_port.h, etc.
PORTABLE_MM_NET_ARCH_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]arch/'
)

KERNEL_HEADER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]kernel/')

DRIVER_BLOCK_DEV_RAW_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]drivers/storage/block_dev\.h[>"]'
)

BLUETOOTH_SUBDIR_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]bluetooth/')

DRIVERS_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')

KERNEL_DRIVER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]drivers/')

KERNEL_ARCH_PORTABLE_DIRECT_RE = re.compile(
    r'^\s*#\s*include\s*[<"]arch/common/arch_portable\.h[>"]'
)

FS_MM_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]mm/')

DEBUG_BINS_TEST_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*"test/')

DEBUG_BINS_KTEST_CMD = ROOT / "debug_bins" / "cmd_ktest.c"

DEVFS_USERCOPY_RE = re.compile(r"\bcopy_(to|from)_user\s*\(")
DEVFS_USERCOPY_WHITELIST = {
    "dev_audio_ioctl",
    "dev_console_ioctl",
    "dev_fb0_ioctl",
    "dev_pty_ioctl",
}

DIRS_BLUETOOTH_INCLUDE_SCAN = [
    ROOT / "arch",
    ROOT / "debug_bins",
    ROOT / "drivers",
    ROOT / "fs",
    ROOT / "includes",
    ROOT / "interrupt",
    ROOT / "kernel",
    ROOT / "mm",
    ROOT / "net",
]

PORTABLE_DIRS_NO_KERNEL_HEADERS = [
    ROOT / "fs",
    ROOT / "net",
    ROOT / "mm",
    ROOT / "drivers",
]

PORTABLE_DIRS_MM_NET_NO_ARCH = [
    ROOT / "mm",
    ROOT / "net",
]

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


def check_mm_net_no_arch_includes():
    errors = []
    for base in PORTABLE_DIRS_MM_NET_NO_ARCH:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if PORTABLE_MM_NET_ARCH_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[mm-net-no-arch-include] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_portable_trees_no_kernel_headers():
    errors = []
    for base in PORTABLE_DIRS_NO_KERNEL_HEADERS:
        for fpath in iter_c_files(base):
            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if KERNEL_HEADER_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[portable-no-kernel-header] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_drivers_ir0_block_dev_only():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVER_BLOCK_DEV_RAW_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[driver-block-dev-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_drivers_no_arch_includes():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVERS_ARCH_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[drivers-no-arch] {rel}:{idx}: {line.strip()}")
    return errors


def check_kernel_no_driver_includes():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_DRIVER_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[kernel-no-driver-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_kernel_no_direct_arch_portable():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_ARCH_PORTABLE_DIRECT_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[kernel-use-arch-port-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_fs_no_mm_includes():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_MM_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[fs-no-mm-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_debug_bins_test_include_policy():
    errors = []
    base = ROOT / "debug_bins"
    if not base.exists():
        return errors
    for fpath in iter_c_files(base):
        if fpath.resolve() == DEBUG_BINS_KTEST_CMD.resolve():
            continue
        try:
            lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DEBUG_BINS_TEST_INCLUDE_RE.search(line):
                rel = fpath.relative_to(ROOT)
                errors.append(
                    f"[debug-bins-no-test-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_bluetooth_subdir_include_policy():
    errors = []
    bt_root = ROOT / "drivers" / "bluetooth"

    for scan_root in DIRS_BLUETOOTH_INCLUDE_SCAN:
        if not scan_root.exists():
            continue
        for fpath in iter_c_files(scan_root):
            allowed_bluetooth_vendor = False
            try:
                fpath.relative_to(bt_root)
                allowed_bluetooth_vendor = True
            except ValueError:
                allowed_bluetooth_vendor = False

            # Allow subsystem-local includes inside drivers/bluetooth only.
            if allowed_bluetooth_vendor:
                continue

            try:
                lines = fpath.read_text(encoding="utf-8", errors="replace").splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue

            for idx, line in enumerate(lines, start=1):
                if BLUETOOTH_SUBDIR_INCLUDE_RE.search(line):
                    rel = fpath.relative_to(ROOT)
                    errors.append(
                        f"[bluetooth-include-scope] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_devfs_usercopy_contract():
    errors = []
    devfs_path = ROOT / "fs" / "devfs.c"
    if not devfs_path.exists():
        return errors

    try:
        lines = devfs_path.read_text(encoding="utf-8", errors="replace").splitlines()
    except Exception as exc:
        errors.append(f"[read-error] {devfs_path}: {exc}")
        return errors

    current_fn = None
    fn_re = re.compile(r'^\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(')

    for idx, line in enumerate(lines, start=1):
        m = fn_re.match(line)
        if m:
            name = m.group(1)
            if name not in ("if", "while", "for", "switch", "return", "sizeof"):
                current_fn = name
        if DEVFS_USERCOPY_RE.search(line):
            if current_fn not in DEVFS_USERCOPY_WHITELIST:
                rel = devfs_path.relative_to(ROOT)
                errors.append(
                    f"[devfs-io-contract-usercopy] {rel}:{idx}: {line.strip()} (fn={current_fn})"
                )
    return errors


def check_ktm_core_no_fase():
    """KTM v1 core must not embed legacy FASE diagnostics."""
    errors = []
    roots = [
        ROOT / "includes" / "ir0" / "ktm",
        ROOT / "ktm" / "event_ring.c",
        ROOT / "ktm" / "transport_serial.c",
        ROOT / "ktm" / "registry.c",
        ROOT / "ktm" / "snapshot.c",
        ROOT / "ktm" / "assert.c",
        ROOT / "ktm" / "checkpoint.c",
        ROOT / "ktm" / "fault.c",
        ROOT / "ktm" / "scenario.c",
        ROOT / "ktm" / "invariant_global.c",
        ROOT / "ktm" / "scenarios",
        ROOT / "ktm" / "userdev.c",
    ]
    fase_re = re.compile(r"FASE[0-9]|\[FASE")
    for base in roots:
        for fpath in iter_c_files(base):
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                rel = fpath.relative_to(ROOT)
                errors.append(f"[ktm-no-fase] {rel}: FASE markers forbidden in KTM core")
    return errors


def check_ktm_no_fase_serial():
    """Forbid any [FASE serial markers in kernel trees (KTM is sole source of truth)."""
    errors = []
    fase_re = re.compile(r"\[FASE")
    scan_roots = [
        ROOT / "kernel",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "drivers",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "arch",
        ROOT / "sched",
    ]
    for base in scan_roots:
        if not base.exists():
            continue
        for fpath in iter_c_files(base):
            rel = str(fpath.relative_to(ROOT)).replace("\\", "/")
            try:
                text = fpath.read_text(encoding="utf-8", errors="replace")
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                errors.append(
                    f"[ktm-no-fase] {rel}: [FASE serial forbidden; use KTM checkpoints/events"
                )
    return errors


def main():
    errors = []
    errors.extend(check_forbidden_includes())
    errors.extend(check_facades())
    errors.extend(check_arm64_scaffold())
    errors.extend(check_interrupt_arch_portable())
    errors.extend(check_fs_no_direct_arch())
    errors.extend(check_mm_net_no_arch_includes())
    errors.extend(check_portable_trees_no_kernel_headers())
    errors.extend(check_drivers_ir0_block_dev_only())
    errors.extend(check_drivers_no_arch_includes())
    errors.extend(check_kernel_no_driver_includes())
    errors.extend(check_kernel_no_direct_arch_portable())
    errors.extend(check_fs_no_mm_includes())
    errors.extend(check_debug_bins_test_include_policy())
    errors.extend(check_bluetooth_subdir_include_policy())
    errors.extend(check_devfs_usercopy_contract())
    errors.extend(check_ktm_core_no_fase())
    errors.extend(check_ktm_no_fase_serial())

    if errors:
        print("[arch-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[arch-guard] OK")
    print("DEVFS_IO_CONTRACT_OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
