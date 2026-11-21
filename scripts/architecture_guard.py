#!/usr/bin/env python3
"""
IR0 architecture guardrails.

Checks:
1) No direct <drivers/...> includes inside fs/* and kernel/syscalls/.
2) Required facade headers for subsystem decoupling exist.
3) Portable trees (fs/kernel/mm/net) must not include arch interrupt headers
   (#include <interrupt/...> or <arch/.../interrupt/...>);
4) Files under fs/ must not use #include <arch/...>; use includes/ir0/* facades instead.
5) fs/, mm/, net/, drivers/: no #include <kernel/*.h> (no whitelist).
6) mm/, net/, sched/: no #include <arch/...>.
7) drivers/: no raw #include of drivers/storage/block_dev.h — use ir0/blockdev.h.
8) Paths outside drivers/bluetooth/ must not #include bluetooth/...
9) drivers/: no #include <arch/...> (use ir0/arch_port.h).
10) kernel/: no #include <drivers/...> (whole tree).
11) kernel/: no #include <arch/common/arch_portable.h> (use ir0/arch_port.h).
12) fs/: no #include <mm/...> (use ir0/mm_port.h or narrower facades).
13) Portable trees must not embed ISA asm (pause/hlt/cli/port IO/CR/DR/…);
    use simple facades in includes/ir0/cpu.h (smp_mb, inb, udelay, …).
    Empty barrier OK.
14) Portable trees must not hardcode userspace RIPs (0x402B*) or read CR2
    / emit iretq/sysret mnemonics outside arch/ (use arch_page_fault /
    arch_fork facades).
15) includes/ir0/*.h must not #include <drivers/...> (facade seal).
16) includes/ir0/*.h must not #include <arch/...> or <sched/...> (facade seal).
17) includes/ir0/*.h must not #include <kernel/...> except ARCH-STAB allowlist (6 passthrough headers).
18) process_t.page_directory must not be touched outside process.h /
    mm_struct.c — use process_pgd() / process_set_pgd() (mm->page_directory OK).
19) If kernel/lib/ exists, selected syscall/MM/IPC helper .c files must not duplicate the same
    basename under includes/ir0/ (I1–I2 migration duplicate check).
20) Portable task setup must not construct x86 RFLAGS directly; use task_ops.
21) Process lifecycle C must not select an ISA with compiler predefines.
22) ISA assembly may call only its reviewed C boundary callbacks.
23) Common MM requests TLB coherence through the address-space-aware facade.
24) Error numbers are defined only by includes/errno.h; subsystems consume the
    canonical catalog through <errno.h> or <ir0/errno.h>.
25) Portable .c under drivers/fs/kernel/mm/net/sched/ipc/ktm/interrupt must not
    use #if* on compiler ISA or CONFIG_ARCH_* tokens; use build-selected
    backends or platform objects (scripts/make/platform/<platform>.mk). Ignore
    // line comments when matching (avoids flagging examples in comments).
26) Include layout: a C/H file includes each header once, and every #include
    comes before the first function definition (no late includes).
    `--self-test` injects both violations into scratch files and checks that
    the layout check reports them.
27) No busy-wait primitive: cpu_relax() is retired, the terminal halt
    cpu_halt() is called only from the panic / power-off / kexec paths and
    declared only in <ir0/oops_port.h>, and the idle wake-polling names
    (kernel_idle_poll, process_idle_bind, ...) stay gone. The check runs an
    injected-violation self-test first.
28) No raw atomics: __atomic_*, __sync_* and inline asm appear only under
    arch/ and in includes/asm-generic/atomic.h; portable code uses
    <ir0/atomic.h>. Empty exception ledger; injected-violation self-test.
29) No undefined prototypes: every function a project header declares is
    defined somewhere in the tree (C, C++, Rust or an assembly label,
    macro-generated labels included). Exceptions live in
    scripts/undefined_prototypes.txt, which only shrinks; Linux UAPI headers
    and the userspace wrapper header are not scanned. Injected-violation
    self-test.
30) Legacy PC is a platform, not a branch: no #if/#ifdef/#define names
    IR0_LEGACY_PC_IO or another PC-platform switch, and port I/O
    (inb/outb/inw/outw/inl/outl, ins*/outs*, io_wait) is called or defined
    only under arch/x86-64/ and in the sources scripts/make/platform/pc.mk
    builds. No ledger; injected-violation self-test.
31) Domain isolation: a member of an isolated domain (today each
    filesystem in fs/<name>/ with its scripts/make/fs/<name>.mk) includes
    no header of another member, uses no symbol another member defines and
    tests no CONFIG_ENABLE_FS_<other> / CONFIG_FS_<other>; the domain core
    (files directly in fs/, scripts/make/fs.mk) tests no member CONFIG
    symbol and includes no member header. New domains (net/ protocols,
    drivers/ classes) are one DomainSpec. No ledger; injected-violation
    self-test.
32) kernel/ keeps one loose file: kernel/main.c is the only .c/.h/.S/.asm
    directly under kernel/; everything else lives in a kernel/<responsibility>/
    subdirectory, lib/ or its owner subsystem. No ledger; injected-violation
    self-test.
33) Every file in Documentation/ai_driven_dev/rules/ has a "### <file name>"
    section in rules-digest.md and every section names an existing rule
    file. No ledger; injected-violation self-test.
34) Scheduler policies share one private contract. A sched/*.c other than
    core.c, idle.c and trace.c that defines sched_pick_next also defines
    sched_add, sched_remove, sched_promote, sched_count_runnable,
    sched_pick_next, sched_policy_name and sched_selftest; it does not
    define schedule or sched_switch_to, and it does not include rr.c,
    prio.c, cfs.c or idle.c. includes/ir0/sched.h keeps the public set
    and does not declare sched_pick_next. No ledger; injected-violation
    self-test.
"""

from functools import lru_cache
from pathlib import Path
from typing import Optional
import json
import os
import sys
import re
import tempfile


ROOT = Path(__file__).resolve().parent.parent

# Generated, vendored and VCS trees are not IR0 sources. Skipping them keeps
# `make arch-guard` fast (the toolchain alone carries >1k C/H files).
ROOT_PREFIX = str(ROOT) + os.sep
SKIP_DIR_NAMES = frozenset(
    {
        ".git",
        ".hg",
        ".svn",
        ".cache",
        ".venv",
        "venv",
        "__pycache__",
        "artifacts",
        "build",
        "dist",
        "iso",
        "node_modules",
        "out",
        "toolchain",
    }
)

FORBIDDEN_PATHS = [
    ROOT / "fs",
    ROOT / "kernel" / "syscalls",
]

REQUIRED_FACADES = [
    ROOT / "includes" / "ir0" / "driver.h",
    ROOT / "includes" / "ir0" / "driver_bootstrap.h",
    ROOT / "includes" / "ir0" / "blockdev.h",
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
    ROOT / "includes" / "ir0" / "sched.h",
    ROOT / "includes" / "ir0" / "pseudo_fs.h",
    ROOT / "includes" / "ir0" / "cred.h",
    ROOT / "includes" / "ir0" / "ktm" / "ktm.h",
    ROOT / "includes" / "ir0" / "irq.h",
    ROOT / "includes" / "ir0" / "context_backend.h",
    ROOT / "includes" / "ir0" / "arch_mm.h",
    ROOT / "includes" / "ir0" / "task_ops.h",
    ROOT / "includes" / "ir0" / "fork.h",
    ROOT / "includes" / "ir0" / "page_fault.h",
    ROOT / "includes" / "ir0" / "signal_irq.h",
    ROOT / "includes" / "ir0" / "arch_elf.h",
    ROOT / "includes" / "ir0" / "syscall_frame.h",
]

REQUIRED_ARM64_SCAFFOLD = [
    ROOT / "arch" / "arm64" / "linker.ld",
    ROOT / "arch" / "arm64" / "sources" / "exc_early.c",
    ROOT / "drivers" / "tty" / "pl011.c",
    ROOT / "drivers" / "tty" / "arm_serial.c",
    ROOT / "arch" / "arm64" / "sources" / "mm_ops.c",
    ROOT / "drivers" / "irq" / "gic_v2.c",
    ROOT / "arch" / "arm64" / "sources" / "timer.c",
    ROOT / "arch" / "arm64" / "asm" / "entry" / "vectors.S",
    ROOT / "arch" / "arm64" / "sources" / "early.c",
    ROOT / "arch" / "arm64" / "sources" / "interrupts.c",
]

DRIVER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]drivers/')
FACADE_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')
FACADE_SCHED_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]sched/')

# Arch interrupt backends are hardware-specific; portable code uses ir0/irq.h.
INTERRUPT_ARCH_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](?:interrupt/|arch/[^/">]+/(?:sources|include)/interrupt/)'
)

# fs/ pseudo-VFS layers must route CPU/HW probes through ir0/*.h wrappers.
FS_DIRECT_ARCH_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]arch/')

# mm/ and net/ must not pull arch headers directly — use ir0/arch_port.h, etc.
PORTABLE_MM_NET_ARCH_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"]arch/'
)

KERNEL_HEADER_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]kernel/')

# Facade headers allowed to pass a kernel/ header through. Empty: <ir0/process.h>
# is the process header itself, not a passthrough to kernel/process.h.
FACADE_KERNEL_INCLUDE_ALLOWLIST: frozenset[str] = frozenset()

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
LEGACY_TLS_CONTRACT_RE = re.compile(
    r"\b(?:set_fs_base|get_fs_base|restore_user_fs_base)\b"
)
FD_LIFECYCLE_SUBTYPE_RE = re.compile(
    r"\bis_(?:pipe|socket|devfs|pseudo|epoll|memfd|eventfd|timerfd)\b"
)

DEVFS_USERCOPY_RE = re.compile(r"\bcopy_(to|from)_user\s*\(")
DEVFS_USERCOPY_WHITELIST = {
    "dev_audio_ioctl",
    "dev_console_ioctl",
    "dev_events0_ioctl",
    "dev_fb0_ioctl",
    "dev_mouse_ioctl",
    "dev_net_ioctl",
    "dev_pty_ioctl",
    "pty_master_ioctl_pair",
}

DIRS_BLUETOOTH_INCLUDE_SCAN = [
    ROOT / "arch",
    ROOT / "drivers",
    ROOT / "fs",
    ROOT / "includes",
    ROOT / "interrupt",
    ROOT / "kernel",
    ROOT / "lib",
    ROOT / "smp",
    ROOT / "mm",
    ROOT / "net",
]

PORTABLE_DIRS_NO_KERNEL_HEADERS = [
    ROOT / "fs",
    ROOT / "net",
    ROOT / "mm",
    ROOT / "drivers",
    ROOT / "ipc",
]

PORTABLE_DIRS_MM_NET_NO_ARCH = [
    ROOT / "mm",
    ROOT / "net",
    ROOT / "sched",
]

PORTABLE_DIRS_NO_INTERRUPT_ARCH = [
    ROOT / "fs",
    ROOT / "kernel",
    ROOT / "lib",
    ROOT / "smp",
    ROOT / "mm",
    ROOT / "net",
    ROOT / "drivers",
    ROOT / "sched",
    ROOT / "ipc",
]


@lru_cache(maxsize=None)
def iter_c_files(base: Path) -> tuple[Path, ...]:
    """Return the C/H files under *base*, pruned and cached for this run."""
    if base.is_file():
        return (base,)
    files: list[Path] = []
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIR_NAMES)
        for name in sorted(filenames):
            if name.endswith((".c", ".h")):
                files.append(Path(dirpath) / name)
    return tuple(files)


@lru_cache(maxsize=None)
def read_file_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace")


@lru_cache(maxsize=None)
def rel_to_root(path: Path) -> Path:
    """Memoized relative path; string slicing beats pathlib.relative_to here."""
    raw = str(path)
    if raw.startswith(ROOT_PREFIX):
        return Path(raw[len(ROOT_PREFIX):])
    raise ValueError(f"{path} is not under {ROOT}")


def check_forbidden_includes():
    errors = []
    for target in FORBIDDEN_PATHS:
        for fpath in iter_c_files(target):
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = rel_to_root(fpath)
            for idx, line in enumerate(lines, start=1):
                if DRIVER_INCLUDE_RE.search(line):
                    errors.append(f"[forbidden-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_facades():
    errors = []
    for facade in REQUIRED_FACADES:
        if not facade.exists():
            rel = facade.relative_to(ROOT)
            errors.append(f"[missing-facade] {rel}")
    return errors


def check_semantic_tls_contract():
    errors = []
    for fpath in (ROOT / "includes" / "ir0").glob("*.h"):
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(),
            start=1,
        ):
            if LEGACY_TLS_CONTRACT_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(f"[legacy-tls-contract] {rel}:{idx}: {line.strip()}")
    return errors


def check_process_fd_lifecycle_boundary():
    errors = []
    for rel in ("fs/files_struct.c", "fs/fdtable.c"):
        fpath = ROOT / rel
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(),
            start=1,
        ):
            if FD_LIFECYCLE_SUBTYPE_RE.search(line):
                errors.append(f"[fd-lifecycle-coupling] {rel}:{idx}: {line.strip()}")
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
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = rel_to_root(fpath)
            for idx, line in enumerate(lines, start=1):
                if INTERRUPT_ARCH_INCLUDE_RE.search(line):
                    errors.append(
                        f"[portable-no-interrupt-arch] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_fs_no_direct_arch():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        rel = rel_to_root(fpath)
        for idx, line in enumerate(lines, start=1):
            if FS_DIRECT_ARCH_INCLUDE_RE.search(line):
                errors.append(f"[fs-no-direct-arch] {rel}:{idx}: {line.strip()}")
    return errors


def check_mm_net_no_arch_includes():
    errors = []
    for base in PORTABLE_DIRS_MM_NET_NO_ARCH:
        for fpath in iter_c_files(base):
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = rel_to_root(fpath)
            for idx, line in enumerate(lines, start=1):
                if PORTABLE_MM_NET_ARCH_INCLUDE_RE.search(line):
                    tag = (
                        "[sched-no-arch-include]"
                        if base.name == "sched"
                        else "[mm-net-no-arch-include]"
                    )
                    errors.append(f"{tag} {rel}:{idx}: {line.strip()}")
    return errors


def check_facade_no_drivers_include():
    """ir0 facade *headers* must not pull drivers/arch/sched; .c adapters may."""
    errors = []
    base = ROOT / "includes" / "ir0"
    if not base.is_dir():
        return errors
    for fpath in base.rglob("*.h"):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        rel = rel_to_root(fpath)
        for idx, line in enumerate(lines, start=1):
            if DRIVER_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-drivers-include] {rel}:{idx}: {line.strip()}"
                )
            if FACADE_ARCH_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-arch-include] {rel}:{idx}: {line.strip()}"
                )
            if FACADE_SCHED_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-sched-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_facade_no_kernel_include():
    """includes/ir0/*.h must not passthrough #include <kernel/...> (allowlist only)."""
    errors = []
    base = ROOT / "includes" / "ir0"
    if not base.is_dir():
        return errors
    for fpath in base.rglob("*.h"):
        try:
            rel_s = rel_to_root(fpath).as_posix()
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        if rel_s in FACADE_KERNEL_INCLUDE_ALLOWLIST:
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_HEADER_INCLUDE_RE.search(line):
                errors.append(
                    f"[facade-no-kernel-include] {rel_s}:{idx}: {line.strip()}"
                )
    return errors


def check_portable_trees_no_kernel_headers():
    errors = []
    for base in PORTABLE_DIRS_NO_KERNEL_HEADERS:
        for fpath in iter_c_files(base):
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, start=1):
                if KERNEL_HEADER_INCLUDE_RE.search(line):
                    rel = rel_to_root(fpath)
                    errors.append(
                        f"[portable-no-kernel-header] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_drivers_ir0_block_dev_only():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVER_BLOCK_DEV_RAW_INCLUDE_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(
                    f"[driver-block-dev-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


SINGLE_DECL_TREES = (
    "arch", "block", "drivers", "fs", "includes", "ipc", "kernel", "ktm",
    "lib", "mm", "net", "sched", "smp", "sound", "tests/ktm",
)
_DECL_COMMENT_RE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)
_DECL_PP_RE = re.compile(r"^[ \t]*#(?:[^\n]*\\\n)*[^\n]*", re.M)
_DECL_PROTO_RE = re.compile(
    r"^(?:extern\s+)?[A-Za-z_][\w\s\*]*?\b([A-Za-z_]\w*)\s*\([^()]*\)"
    r"\s*(?:__attribute__\s*\(\(.*\)\)\s*)*$",
    re.S,
)


def _header_prototypes(text: str) -> set[str]:
    """Names of non-static function prototypes at file scope of a header."""
    text = _DECL_PP_RE.sub("", _DECL_COMMENT_RE.sub(" ", text))
    names: set[str] = set()
    depth = 0
    stmt: list[str] = []
    for ch in text:
        if ch == "{":
            depth += 1
            stmt.clear()
        elif ch == "}":
            depth = max(depth - 1, 0)
            stmt.clear()
        elif depth == 0 and ch == ";":
            decl = " ".join("".join(stmt).split())
            stmt.clear()
            if (not decl or decl.startswith(("static", "typedef"))
                    or "(*" in decl.replace(" ", "")):
                continue
            match = _DECL_PROTO_RE.match(decl)
            if match:
                names.add(match.group(1))
        elif depth == 0:
            stmt.append(ch)
    return names


def check_single_header_declarations():
    """Each kernel function is declared in exactly one header, never in a .c.

    Private headers include their includes/ir0 facade (or the owning sibling
    header) instead of re-declaring it, so prototypes cannot drift apart. A
    prototype written into a .c file is never checked against the definition
    and often hides a layering shortcut; it belongs in the owner's header
    (asm entry points included).
    """
    errors = []
    owners: dict[str, list[str]] = {}
    for tree in SINGLE_DECL_TREES:
        for fpath in iter_c_files(ROOT / tree):
            try:
                text = read_file_text(fpath)
            except OSError as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = str(rel_to_root(fpath))
            for name in sorted(_header_prototypes(text)):
                if fpath.suffix == ".h":
                    owners.setdefault(name, []).append(rel)
                else:
                    errors.append(
                        f"[c-file-prototype] {rel}: {name} "
                        "(declare it in the owner's header and include that)"
                    )
    for name in sorted(owners):
        if len(owners[name]) > 1:
            errors.append(
                f"[duplicate-declaration] {name}: "
                + ", ".join(sorted(owners[name]))
                + " (keep one owner header; include it elsewhere)"
            )
    return errors


def check_drivers_no_arch_includes():
    errors = []
    base = ROOT / "drivers"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if DRIVERS_ARCH_INCLUDE_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(f"[drivers-no-arch] {rel}:{idx}: {line.strip()}")
    return errors


def check_kernel_no_driver_includes():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_DRIVER_INCLUDE_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(
                    f"[kernel-no-driver-include] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_kernel_no_direct_arch_portable():
    errors = []
    base = ROOT / "kernel"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if KERNEL_ARCH_PORTABLE_DIRECT_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(
                    f"[kernel-use-arch-port-facade] {rel}:{idx}: {line.strip()}"
                )
    return errors


def check_fs_no_mm_includes():
    errors = []
    base = ROOT / "fs"
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_MM_INCLUDE_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(f"[fs-no-mm-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_ktm_no_mm_includes():
    """KTM observes IR0 via facades — no direct <mm/...> includes."""
    errors = []
    base = ROOT / "ktm"
    if not base.is_dir():
        return errors
    for fpath in iter_c_files(base):
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, start=1):
            if FS_MM_INCLUDE_RE.search(line):
                rel = rel_to_root(fpath)
                errors.append(f"[ktm-no-mm-include] {rel}:{idx}: {line.strip()}")
    return errors


def check_bluetooth_subdir_include_policy():
    errors = []
    bt_root = ROOT / "drivers" / "bluetooth"

    for scan_root in DIRS_BLUETOOTH_INCLUDE_SCAN:
        if not scan_root.exists():
            continue
        for fpath in iter_c_files(scan_root):
            if str(fpath).startswith(str(bt_root) + os.sep):
                continue

            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue

            for idx, line in enumerate(lines, start=1):
                if "bluetooth/" not in line:
                    continue
                if BLUETOOTH_SUBDIR_INCLUDE_RE.search(line):
                    rel = rel_to_root(fpath)
                    errors.append(
                        f"[bluetooth-include-scope] {rel}:{idx}: {line.strip()}"
                    )
    return errors


def check_devfs_usercopy_contract():
    errors = []
    devfs_paths = [ROOT / "fs" / "devfs.c", ROOT / "fs" / "devfs_pty.c"]
    current_fn = None
    fn_re = re.compile(
        r'^\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+'
        r'([A-Za-z_][A-Za-z0-9_]*)\s*\('
    )

    for devfs_path in devfs_paths:
        if not devfs_path.exists():
            continue

        try:
            lines = devfs_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {devfs_path}: {exc}")
            continue

        current_fn = None
        for idx, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            m = fn_re.match(line)
            if m and not stripped.startswith("return"):
                name = m.group(1)
                if name not in ("if", "while", "for", "switch", "return", "sizeof"):
                    current_fn = name
            if DEVFS_USERCOPY_RE.search(line):
                if current_fn not in DEVFS_USERCOPY_WHITELIST:
                    rel = rel_to_root(devfs_path)
                    errors.append(
                        f"[devfs-io-contract-usercopy] {rel}:{idx}: {line.strip()} "
                        f"(fn={current_fn})"
                    )
    return errors


def check_usercopy_no_raw_user_touch():
    """
    Forbid CR3-switch + raw memcpy/memset into (void *) user VAs, and
    sys_uname-style memset/strncpy into the syscall buffer argument.
    """
    errors = []
    fn_re = re.compile(
        r"^\s*(?:static\s+)?(?:inline\s+)?[A-Za-z_][A-Za-z0-9_\s\*]*\s+"
        r"([A-Za-z_][A-Za-z0-9_]*)\s*\("
    )
    cr3_memcpy_re = re.compile(r"memcpy\s*\(\s*\(void\s*\*\)")
    cr3_memset_re = re.compile(r"memset\s*\(\s*\(void\s*\*\)")
    load_cr3_re = re.compile(r"load_page_directory\s*\(")
    uname_memset_re = re.compile(r"memset\s*\(\s*buf\s*,")
    uname_strncpy_re = re.compile(r"strncpy\s*\(\s*buf\s*->")

    targets = [ROOT / "kernel" / "lib" / "signals.c"]
    syscalls = ROOT / "kernel" / "syscalls"
    if syscalls.is_dir():
        targets.extend(sorted(syscalls.glob("*.c")))

    for fpath in targets:
        if not fpath.is_file():
            continue
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue

        current_fn = None
        saw_load_cr3 = False
        rel = rel_to_root(fpath)

        for idx, line in enumerate(lines, start=1):
            stripped = line.lstrip()
            if stripped.startswith("//") or stripped.startswith("/*"):
                continue
            m = fn_re.match(line)
            if m and not stripped.startswith("return"):
                name = m.group(1)
                if name not in ("if", "while", "for", "switch", "return", "sizeof"):
                    current_fn = name
                    saw_load_cr3 = False

            if load_cr3_re.search(line):
                saw_load_cr3 = True

            if saw_load_cr3 and (
                cr3_memcpy_re.search(line) or cr3_memset_re.search(line)
            ):
                errors.append(
                    f"[usercopy-no-cr3-memcpy] {rel}:{idx}: "
                    f"load_page_directory + raw mem* in {current_fn}; "
                    f"use copy_to_user / *_region_in_directory"
                )

            if current_fn == "sys_uname" and (
                uname_memset_re.search(line) or uname_strncpy_re.search(line)
            ):
                errors.append(
                    f"[usercopy-sys-uname] {rel}:{idx}: "
                    f"write utsname via kernel bounce + copy_to_user"
                )

            # signals.c: any memcpy((void *) is the known KERNEL_UACCESS pattern
            if fpath.name == "signals.c" and cr3_memcpy_re.search(line):
                errors.append(
                    f"[usercopy-signals] {rel}:{idx}: "
                    f"memcpy((void *) forbidden; use copy_to_user_mm"
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
                text = read_file_text(fpath)
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                rel = rel_to_root(fpath)
                errors.append(f"[ktm-no-fase] {rel}: FASE markers forbidden in KTM core")
    return errors


def check_ktm_no_fase_serial():
    """Forbid any [FASE serial markers in kernel trees (KTM is sole source of truth)."""
    errors = []
    fase_re = re.compile(r"\[FASE")
    scan_roots = [
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "drivers",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "arch",
        ROOT / "sched",
        ROOT / "ipc",
    ]
    for base in scan_roots:
        if not base.exists():
            continue
        for fpath in iter_c_files(base):
            rel = str(rel_to_root(fpath)).replace("\\", "/")
            try:
                text = read_file_text(fpath)
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if fase_re.search(text):
                errors.append(
                    f"[ktm-no-fase] {rel}: [FASE serial forbidden; use KTM checkpoints/events"
                )
    return errors


def check_klog_serial_print_allowlist():
    """
    Human logging must go through KTM hub (ktm/klog.c / kprintf / klog_*).
    serial_print is only allowed in the printk serial console
    (kernel/printk/console.c), serial drivers, KTM protocol
    transport, and remaining gated diag TUs (shrinking allowlist).
    """
    errors = []
    allow = {
        "kernel/printk/console.c",
        "ktm/transport_serial.c",
        "drivers/serial/serial.c",
        "drivers/serial/serial.h",
        "includes/ir0/serial_io.h",
        "drivers/tty/arm_serial.c",
        "tests/host/host_serial_stub.c",
    }
    serial_re = re.compile(r"\bserial_print\s*\(")
    scan_roots = [
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "drivers",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "arch",
        ROOT / "sched",
        ROOT / "interrupt",
        ROOT / "ipc",
    ]
    for base in scan_roots:
        if not base.exists():
            continue
        for fpath in iter_c_files(base):
            rel = str(rel_to_root(fpath)).replace("\\", "/")
            if rel in allow:
                continue
            if "/third-party/" in rel:
                continue
            try:
                text = read_file_text(fpath)
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(text.splitlines(), 1):
                if _line_is_comment_only(line):
                    continue
                if "serial_print" not in line:
                    continue
                if serial_re.search(line):
                    errors.append(
                        f"[klog-hub] {rel}:{idx}: use kprintf/klog_* "
                        f"(serial_print only in the printk serial console + serial/KTM protocol)"
                    )
    return errors


def check_ktm_angle_includes():
    """KTM sources must use <ktm_…> / <ir0/ktm/…>, not relative or quoted ktm paths."""
    errors = []
    bad_re = re.compile(
        r'^\s*#\s*include\s+("(\.\./)+.*ktm[^"]*"|'
        r'"ktm_internal\.h"|'
        r'"\.\./ktm_internal\.h"|'
        r'<(\.\./)+.*ktm[^>]*>)'
    )
    # Also catch facade relative include of ktm/include
    facade_rel_re = re.compile(
        r'^\s*#\s*include\s+"(\.\./)+ktm/'
    )
    scan = [
        ROOT / "ktm",
        ROOT / "includes" / "ir0" / "ktm.h",
        ROOT / "includes" / "ir0" / "ktm",
    ]
    for base in scan:
        for fpath in iter_c_files(base):
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = rel_to_root(fpath)
            for idx, line in enumerate(lines, 1):
                if bad_re.search(line) or facade_rel_re.search(line):
                    errors.append(
                        f"[ktm-include] {rel}:{idx}: use <ktm_…> or <ir0/ktm/…> "
                        f"(no relative/quoted ktm paths): {line.strip()}"
                    )
    return errors


def check_kernel_no_relative_includes():
    """kernel/syscalls and kernel/process must not use #include \"../…\"."""
    errors = []
    bad_re = re.compile(r'^\s*#\s*include\s+"\.\./')
    for base in (ROOT / "kernel" / "syscalls", ROOT / "kernel" / "process"):
        for fpath in iter_c_files(base):
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            rel = rel_to_root(fpath)
            for idx, line in enumerate(lines, 1):
                if bad_re.search(line):
                    errors.append(
                        f"[kernel-include] {rel}:{idx}: use <kernel/…> "
                        f"(no relative includes): {line.strip()}"
                    )
    return errors


# Portable C must not embed ISA asm — facades only (simple names in ir0/cpu.h).
# Allow: empty compiler barrier asm volatile("" ::: "memory")
# Allowlist: per-ISA userspace syscall stubs (selected by build, not portable C).
PORTABLE_ISA_ASM_RE = re.compile(
    r"(?:__asm__|asm)\s*(?:volatile)?\s*\([^;]*"
    r"(?:pause|hlt|cli|sti|rdtsc|mfence|lfence|sfence|cpuid|rdmsr|wrmsr|"
    r"invlpg|pushfq|lidt|wfi|yield|dmb|dsb|isb|"
    r"in[bwl]\b|out[bwl]\b|"
    r"%%cr[0-4]|%%dr[0-7]|%%rax|movq\s+%%|"
    r"svc\s+#|syscall\b|sysret|sysenter|sysexit|"
    r"mrs\b|msr\b)",
    re.IGNORECASE,
)
PORTABLE_ISA_TREES = (
    ROOT / "arch" / "common",
    ROOT / "kernel",
    ROOT / "lib",
    ROOT / "smp",
    ROOT / "net",
    ROOT / "includes" / "ir0",
    ROOT / "mm",
    ROOT / "sched",
    ROOT / "drivers",
    ROOT / "fs",
    ROOT / "ipc",
)
PORTABLE_ISA_ALLOWLIST: set[Path] = set()

# ISA firmware headers must not leak into portable policy trees.
PORTABLE_NO_ISA_FIRMWARE_TREES = (
    ROOT / "kernel",
    ROOT / "lib",
    ROOT / "smp",
    ROOT / "fs",
    ROOT / "mm",
    ROOT / "net",
    ROOT / "sched",
    ROOT / "ipc",
    ROOT / "includes" / "ir0",
)
PORTABLE_ISA_FIRMWARE_INCLUDE_RE = re.compile(
    r'^\s*#\s*include\s*[<"](?:ir0/)?(?:arm64_board|multiboot)\.h[>"]'
)
PORTABLE_ISA_FIRMWARE_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "multiboot.h",
}


def check_portable_no_isa_firmware_headers():
    """Ban Multiboot/arm64_board includes from portable policy trees."""
    errors = []
    for base in PORTABLE_NO_ISA_FIRMWARE_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in PORTABLE_ISA_FIRMWARE_ALLOWLIST:
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PORTABLE_ISA_FIRMWARE_INCLUDE_RE.search(line):
                    errors.append(
                        f"[portable-no-isa-firmware] {rel}:{idx}: use "
                        f"boot_memory / platform facades; no Multiboot or "
                        f"arm64_board in portable code: {line.strip()}"
                    )
    return errors



@lru_cache(maxsize=None)
def _line_is_comment_only(line: str) -> bool:
    s = line.strip()
    return not s or s[0] in "/*"


@lru_cache(maxsize=None)
def _is_empty_compiler_barrier(line: str) -> bool:
    """Permit asm volatile(\"\" ::: \"memory\") — not ISA-specific."""
    if '""' not in line:
        return False
    s = re.sub(r"\s+", "", line)
    return bool(
        re.search(
            r'(?:__asm__|asm)(?:volatile)?\(""(?:::+"memory")?\);?',
            s,
            re.IGNORECASE,
        )
    )


def check_portable_no_isa_asm():
    """Fail if portable trees embed ISA mnemonics; use smp_mb/udelay/…"""
    errors = []
    for base in PORTABLE_ISA_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in PORTABLE_ISA_ALLOWLIST:
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if "arch/" in rel_s:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if "asm" not in line.lower():
                    continue
                if _is_empty_compiler_barrier(line):
                    continue
                if PORTABLE_ISA_ASM_RE.search(line):
                    errors.append(
                        f"[portable-no-isa-asm] {rel}:{idx}: use simple "
                        f"facades (smp_mb/udelay/inb/timer_read/…); "
                        f"no ISA asm: {line.strip()}"
                    )
    return errors


# Hardcoded musl/BusyBox RIPs from historical fork bugs — forbidden in product.
PORTABLE_HARDCODED_RIP_RE = re.compile(r"\b0x402[Bb][0-9A-Fa-f]+\b")
# Direct CR2 / iretq / sysret in executable code (not comments).
PORTABLE_CR2_RE = re.compile(r"\b(?:read_cr2|__read_cr2|get_cr2)\s*\(|\bmov\s+.*,\s*cr2\b", re.I)
PORTABLE_IRETQ_SYSRET_ASM_RE = re.compile(
    r'(?:__asm__|asm)\b[^;]*\b(?:iretq|sysret)\b', re.I
)
PORTABLE_X86_STATUS_RE = re.compile(
    r"\b(?:RFLAGS_IF|ir0_rflags_sanitize_user)\b"
)
PORTABLE_X86_STATUS_ALLOWLIST = {
    ROOT / "kernel" / "lib" / "debug_trap.c",
    ROOT / "includes" / "ir0" / "debug_trap.h",
}


def check_portable_no_isa_leak_literals():
    """Ban hardcoded userspace RIPs and CR2/iretq/sysret outside arch/."""
    errors = []
    trees = list(PORTABLE_ISA_TREES) + [ROOT / "sched", ROOT / "includes" / "ir0"]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                low = line.lower()
                if not (
                    "0x402" in line
                    or "cr2" in low
                    or "iretq" in low
                    or "sysret" in low
                    or "rflags_if" in low
                    or "ir0_rflags_sanitize_user" in low
                ):
                    continue
                if PORTABLE_HARDCODED_RIP_RE.search(line):
                    errors.append(
                        f"[portable-no-hardcoded-rip] {rel}:{idx}: "
                        f"workload RIP forbidden; use KTM/live IP: {line.strip()}"
                    )
                if PORTABLE_CR2_RE.search(line):
                    errors.append(
                        f"[portable-no-cr2] {rel}:{idx}: use "
                        f"page_fault_decode(); no CR2 in portable code: "
                        f"{line.strip()}"
                    )
                if PORTABLE_IRETQ_SYSRET_ASM_RE.search(line):
                    errors.append(
                        f"[portable-no-iretq-sysret] {rel}:{idx}: iretq/sysret "
                        f"belong in arch/; use facades: {line.strip()}"
                    )
                if (fpath not in PORTABLE_X86_STATUS_ALLOWLIST and
                        PORTABLE_X86_STATUS_RE.search(line)):
                    errors.append(
                        f"[portable-no-x86-status] {rel}:{idx}: use the "
                        f"task_ops status facade; no x86 RFLAGS in portable "
                        f"code: {line.strip()}"
                    )
    return errors


# Direct GPR / control-register fields on task.arch — portable code must use
# includes/ir0/arch_task.h accessors (or arch_task_ops bulk helpers).
PORTABLE_TASK_ARCH_FIELD_RE = re.compile(
    r"(?:\.|->)arch\.(rax|rbx|rcx|rdx|rsi|rdi|rbp|rsp|r8|r9|r10|r11|r12|r13|r14|r15|"
    r"rip|rflags|cs|ss|ds|es|fs|gs|cr0|cr2|cr3|cr4|dr0|dr1|dr2|dr3|dr6|dr7|"
    r"x0|x1|x2|ttbr0_el1|elr_el1|sp_el0|spsr_el1)\b"
)

PORTABLE_TASK_ARCH_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "arch_task.h",
    ROOT / "includes" / "ir0" / "arch_task_ops.h",
}


def check_asm_offsets_sync():
    """C asm_offsets.h constants must match NASM asm_offsets.inc."""
    errors = []
    h_path = ROOT / "arch" / "x86-64" / "include" / "asm" / "asm_offsets.h"
    i_path = ROOT / "arch" / "x86-64" / "include" / "asm" / "asm_offsets.inc"
    if not h_path.is_file() or not i_path.is_file():
        errors.append("[asm-offsets] missing asm_offsets.h or asm_offsets.inc")
        return errors
    h = h_path.read_text(encoding="utf-8", errors="replace")
    inc = i_path.read_text(encoding="utf-8", errors="replace")
    pairs = [
        ("IR0_PROC_FS_BASE_OFFSET", "PROC_FS_BASE_OFFSET"),
        ("IR0_TASK_ARCH_RIP_OFFSET", "TASK_ARCH_RIP_OFFSET"),
        ("IR0_TASK_ARCH_CR3_OFFSET", "TASK_ARCH_CR3_OFFSET"),
        ("IR0_TASK_ARCH_SS_OFFSET", "TASK_ARCH_SS_OFFSET"),
    ]
    for c_name, asm_name in pairs:
        hm = re.search(rf"#define\s+{c_name}\s+(0x[0-9A-Fa-f]+|\d+)", h)
        im = re.search(rf"%define\s+{asm_name}\s+(0x[0-9A-Fa-f]+|\d+)", inc)
        if not hm or not im:
            errors.append(f"[asm-offsets] missing {c_name} / {asm_name}")
            continue
        if int(hm.group(1), 0) != int(im.group(1), 0):
            errors.append(
                f"[asm-offsets] mismatch {c_name}={hm.group(1)} vs "
                f"{asm_name}={im.group(1)}"
            )
    return errors


def check_portable_no_task_arch_fields():
    """Fail if portable trees touch task.arch.<isa-field> directly."""
    errors = []
    trees = list(PORTABLE_ISA_TREES) + [
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in PORTABLE_TASK_ARCH_ALLOWLIST:
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            # ISA dispatchers under sched/switch may still need allowlist if any
            if rel_s.startswith("sched/switch/switch_"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if "arch." not in line:
                    continue
                if PORTABLE_TASK_ARCH_FIELD_RE.search(line):
                    errors.append(
                        f"[portable-no-task-arch-field] {rel}:{idx}: use "
                        f"task_get_*/task_set_* or arch_task_ops; "
                        f"no direct task.arch field: {line.strip()}"
                    )
    return errors


# process_t.page_directory is a private mirror; mm->page_directory is canonical.
PROCESS_PGD_FIELD_RE = re.compile(
    r"(?<![.\w])(?!mm\b)([A-Za-z_][A-Za-z0-9_]*)->page_directory\b"
)

PROCESS_PGD_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "process.h",
    ROOT / "mm" / "mm_struct.c",
    ROOT / "includes" / "ir0" / "mm_struct.h",
}


def check_process_pgd_accessor():
    """Fail if code touches process->page_directory outside mm bind helpers."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in PROCESS_PGD_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if "page_directory" not in line:
                    continue
                if PROCESS_PGD_FIELD_RE.search(line):
                    errors.append(
                        f"[process-pgd-accessor] {rel}:{idx}: use "
                        f"process_pgd()/process_set_pgd(); "
                        f"no direct process->page_directory: {line.strip()}"
                    )
    return errors


RETIRED_SIGNAL_RESUME_RE = re.compile(
    r"\b(handle_signals|signals_prepare_user_return|"
    r"signals_pause_should_interrupt|signals_should_handle_on_run|"
    r"signal_note_syscall_return|kernel_syscall_sleep|want_kernel_ret|"
    r"syscall_resume_rax|kernel_sleep_syscall_frame|irq_frame_saved|"
    r"wait_resume_child_pid|signal_enter_pending|signal_defer_catchable|"
    r"signal_last_delivered|kernel_sleep_interrupted|syscall_block_nr|"
    r"syscall_interrupted|process_after_task_save|"
    r"process_clear_in_thread_syscall_block|"
    r"context_prepare_kernel_resume|IR0_CLASS_B_REPAIR|"
    r"context_backend_inject_kernel_return_fault|"
    r"process_task_kernel_return_state_bad|KTM_EVENT_CTX_USER_IRET)\b"
)


def check_signal_resume_retired():
    """Signals follow Linux: blocked syscalls return -ERESTARTSYS/-EINTR and
    the syscall exit (do_signal) restarts them or enters the handler. The
    blocked-syscall resume machinery it replaced, and the Class B repair of
    the states that machinery produced, must not come back."""
    errors = []
    for base in (
        ROOT / "arch",
        ROOT / "drivers",
        ROOT / "fs",
        ROOT / "includes",
        ROOT / "ipc",
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "ktm",
        ROOT / "mm",
        ROOT / "net",
        ROOT / "sched",
    ):
        for path in iter_c_files(base):
            for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                match = RETIRED_SIGNAL_RESUME_RE.search(line)
                if match:
                    errors.append(
                        f"[signal-resume-retired] {rel_to_root(path)}:{lineno}: "
                        f"{match.group(1)} is retired; block with "
                        f"signal_pending() and return -ERESTARTSYS / -EINTR"
                    )
    return errors


PROCESS_WAIT_FIELD_RE = re.compile(
    r"(?<![.\w])([A-Za-z_][A-Za-z0-9_]*)->"
    r"(wait_blocked|wait_target_pid|wait_options|wait_status_ptr)\b"
)

PROCESS_WAIT_FIELD_ALLOWLIST = {
    ROOT / "includes" / "ir0" / "process.h",
    ROOT / "kernel" / "process" / "wait_state.c",
}


def check_process_wait_state_accessor():
    """Fail if portable code touches process wait4 block fields directly."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
        ROOT / "interrupt",
        ROOT / "drivers",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in PROCESS_WAIT_FIELD_ALLOWLIST:
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if PROCESS_WAIT_FIELD_RE.search(line):
                    errors.append(
                        f"[process-wait-state-accessor] {rel}:{idx}: "
                        f"use process_wait_* helpers; "
                        f"no direct wait field: {line.strip()}"
                    )
    return errors


# syscall_user_frame_t fields — portable code uses process_syscall_* accessors.
_SYSCALL_FRAME_FIELD_NAMES = (
    r"rip|rsp|rflags|rdi|rsi|rdx|r10|r8|r9|rbx|rbp|r12|r13|r14|r15|"
    r"elr|spsr|sp|x0|x1|x2|x3|x4|x5"
)
SYSCALL_FRAME_FIELD_RE = re.compile(
    rf"syscall_frame\.({_SYSCALL_FRAME_FIELD_NAMES})\b"
)
SYSCALL_FRAME_PTR_FIELD_RE = re.compile(
    rf"\bsf->({_SYSCALL_FRAME_FIELD_NAMES})\b"
)

SYSCALL_FRAME_ALLOWLIST = {
    ROOT / "ktm" / "d1_12_read_diag.c",
    ROOT / "ktm" / "d1_16_tty_read_diag.c",
}


def check_syscall_frame_accessor():
    """Fail if portable code opens syscall_frame.* fields directly."""
    errors = []
    trees = [
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "fs",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "includes" / "ir0",
        ROOT / "ktm",
    ]
    for base in trees:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            if fpath in SYSCALL_FRAME_ALLOWLIST:
                continue
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            rel_s = str(rel).replace("\\", "/")
            if rel_s.startswith("arch/"):
                continue
            if fpath.suffix not in (".c", ".h"):
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                if SYSCALL_FRAME_FIELD_RE.search(line) or SYSCALL_FRAME_PTR_FIELD_RE.search(line):
                    errors.append(
                        f"[syscall-frame-accessor] {rel}:{idx}: use "
                        f"process_syscall_ip/sp/arg/set_*; "
                        f"no direct syscall_frame field: {line.strip()}"
                    )
    return errors


def check_subsystems_json_paths():
    """Fail if subsystems.json names sources or subsystems that do not exist.

    The Kconfig generator only warns on a stale path and then emits its object
    anyway, so rot here surfaced as a link error or as a menu entry that builds
    nothing. Seven paths had already gone stale before this check existed.
    """
    errors = []
    manifest = ROOT / "scripts" / "kconfig" / "subsystems.json"
    if not manifest.is_file():
        return errors
    try:
        data = json.loads(manifest.read_text(encoding="utf-8"))
    except Exception as exc:
        return [f"[subsystems-json] {manifest}: unreadable: {exc}"]

    subsystems = data.get("subsystems", {})
    for sid, entry in subsystems.items():
        for arch, files in (entry.get("files") or {}).items():
            for rel in files:
                if not (ROOT / rel).is_file():
                    errors.append(
                        f"[subsystems-json] {sid}[{arch}]: missing source {rel}"
                    )
        for dep in entry.get("dependencies") or []:
            if dep not in subsystems:
                errors.append(
                    f"[subsystems-json] {sid}: depends on undefined subsystem {dep}"
                )

    # Profiles and the layer diagram may not offer a subsystem that is gone.
    for pid, profile in (data.get("profiles") or {}).items():
        for sid in profile.get("subsystems") or []:
            if sid not in subsystems:
                errors.append(
                    f"[subsystems-json] profile {pid}: undefined subsystem {sid}"
                )
    for layer in (data.get("architecture") or {}).get("layers") or []:
        for sid in layer.get("subsystems") or []:
            if sid not in subsystems:
                errors.append(
                    f"[subsystems-json] layer {layer.get('name')}: "
                    f"undefined subsystem {sid}"
                )
    return errors


KERNEL_LIB_I1_SOURCES = (
    "open_flags.c",
    "stat_user.c",
    "path_user.c",
    "utimens.c",
    "exec_read_trace.c",
    "mm_port.c",
    "pipe.c",
    "copy_user.c",
    "oops.c",
    "signals.c",
    "named_fifo.c",
)


def check_includes_ir0_no_c_sources():
    """includes/ir0/ must not host .c implementations (kernel/lib/ only)."""
    errors = []
    inc = ROOT / "includes" / "ir0"
    if not inc.is_dir():
        return errors
    for fpath in inc.rglob("*.c"):
        errors.append(
            f"[includes-migration] {rel_to_root(fpath)}: "
            f"move implementation to kernel/lib/ or subsystem tree"
        )
    return errors


ARCH_PUBLIC_CALL_RE = re.compile(r"\barch_[a-z_][a-z0-9_]*\s*\(")
CONTEXT_BACKEND_INCLUDE_ALLOW = {
    "sched/switch/context_switch.c",
    "arch/x86-64/sources/context.c",
    "arch/x86-64/sources/user_mode.c",
    "arch/arm64/sources/context.c",
    "arch/arm64/sources/first_switch.c",
}

PORTABLE_NO_ARCH_CALL_TREES = [
    ROOT / "mm",
    ROOT / "kernel",
    ROOT / "lib",
    ROOT / "smp",
    ROOT / "fs",
    ROOT / "net",
    ROOT / "sched",
    ROOT / "interrupt",
    ROOT / "ipc",
]


def check_portable_no_arch_prefix_calls():
    """Portable code must not call arch_*(); use simple facades (PORT-2..4)."""
    errors = []
    for base in PORTABLE_NO_ARCH_CALL_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = rel_to_root(fpath)
                rel_s = str(rel).replace("\\", "/")
            except ValueError:
                continue
            try:
                lines = read_file_text(fpath).splitlines()
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            for idx, line in enumerate(lines, 1):
                if _line_is_comment_only(line):
                    continue
                for m in ARCH_PUBLIC_CALL_RE.finditer(line):
                    name = m.group(0).split("(")[0].strip()
                    errors.append(
                        f"[portable-no-arch-call] {rel_s}:{idx}: "
                        f"use simple facade, not {name}()"
                    )
    return errors


CONTEXT_BACKEND_INCLUDE_RE = re.compile(
    r'#\s*include\s*[<"]ir0/context_backend\.h[>"]'
)


def check_context_backend_include_boundary():
    """The context backend hooks are internal to the switch state machine."""
    errors = []
    for fpath in iter_c_files(ROOT):
        try:
            rel_s = str(rel_to_root(fpath)).replace("\\", "/")
        except ValueError:
            continue
        if rel_s in CONTEXT_BACKEND_INCLUDE_ALLOW:
            continue
        try:
            lines = read_file_text(fpath).splitlines()
        except Exception as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        for idx, line in enumerate(lines, 1):
            if _line_is_comment_only(line):
                continue
            if "context_backend.h" not in line:
                continue
            if CONTEXT_BACKEND_INCLUDE_RE.search(line):
                errors.append(
                    f"[context-backend-boundary] {rel_s}:{idx}: "
                    "only sched/switch/ or an ISA backend may include context_backend.h"
                )
    return errors


def check_kernel_lib_i1_migration():
    """If kernel/lib exists, no *.c there may duplicate includes/ir0/*.c (same basename)."""
    errors = []
    lib_dir = ROOT / "kernel" / "lib"
    inc_dir = ROOT / "includes" / "ir0"
    if not lib_dir.is_dir():
        return errors
    for fpath in sorted(lib_dir.glob("*.c")):
        name = fpath.name
        if (inc_dir / name).is_file():
            errors.append(
                f"[includes-migration] duplicate {inc_dir / name}; "
                f"remove includes/ir0 copy"
            )
    return errors


ARCH_MM_LEGACY_RE = re.compile(
    r"\barch_mm_(?:user_root_slots|root_slots|copy_kernel_half)\s*\("
)
ARCH_PF_LEGACY_RE = re.compile(
    r"\barch_page_fault_(?:decode|info)\b|\bstruct arch_page_fault_info\b"
)
PORTABLE_PORT1_TREES = [
    ROOT / "mm",
    ROOT / "kernel" / "process",
    ROOT / "fs",
    ROOT / "net",
    ROOT / "sched",
    ROOT / "ipc",
]


def check_portable_port1_no_legacy_arch_mm():
    """PORT-1: portable code uses mm_* / page_fault_* facades, not arch_mm_* / arch_page_fault_*."""
    errors = []
    for base in PORTABLE_PORT1_TREES:
        if not base.is_dir():
            continue
        for fpath in iter_c_files(base):
            try:
                rel = rel_to_root(fpath)
            except ValueError:
                continue
            try:
                text = read_file_text(fpath)
            except Exception as exc:
                errors.append(f"[read-error] {fpath}: {exc}")
                continue
            if ARCH_MM_LEGACY_RE.search(text):
                errors.append(
                    f"[portable-port1-mm] {rel}: use mm_user_root_slots / "
                    f"mm_copy_kernel_half (arch_mm.h)"
                )
            if ARCH_PF_LEGACY_RE.search(text):
                errors.append(
                    f"[portable-port1-pf] {rel}: use page_fault_decode / "
                    f"struct page_fault_info (arch_page_fault.h)"
                )
    return errors


def check_scheduler_user_return_boundary():
    """Keep raw IRQ frames and signal delivery out of portable scheduling."""
    errors = []
    sched_header = ROOT / "includes" / "ir0" / "sched.h"
    sched_tree = ROOT / "sched"
    irq_dispatch = (
        ROOT / "arch" / "x86-64" / "sources" / "interrupt" /
        "isr_handlers.c"
    )

    header_text = sched_header.read_text(encoding="utf-8", errors="replace")
    for forbidden in (
        "sched_irq_preempt_from_frame",
        "sched_context_switch_skip_prev_save",
        "sched_context_switch_take_skip_prev_save",
    ):
        if forbidden in header_text:
            errors.append(
                f"{sched_header.relative_to(ROOT)}: raw IRQ/context API "
                f"'{forbidden}' must remain ISA-private"
            )

    for path in sched_tree.rglob("*"):
        if path.suffix not in {".c", ".h"}:
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if re.search(r"\bdo_signal\s*\(", text):
            errors.append(
                f"{rel_to_root(path)}: scheduler must not deliver signals; "
                "use the exit-to-user facade"
            )

    irq_text = irq_dispatch.read_text(encoding="utf-8", errors="replace")
    if "#include <ir0/sched.h>" in irq_text:
        errors.append(
            f"{irq_dispatch.relative_to(ROOT)}: IRQ backend must not include "
            "the scheduler facade"
        )
    if re.search(r"\bsched_[A-Za-z0-9_]*\s*\(", irq_text):
        errors.append(
            f"{irq_dispatch.relative_to(ROOT)}: IRQ backend must not invoke "
            "portable scheduler entry points"
        )

    return errors


def check_scheduler_runqueue_lock_boundary():
    """Scheduler core and policy code take the runqueue lock, never raw IRQ masking."""
    errors = []
    sched_tree = ROOT / "sched"
    owner = sched_tree / "core.h"
    raw_irq = re.compile(r"\birq_(?:save|restore)\s*\(")

    for path in sched_tree.rglob("*"):
        if path.suffix not in {".c", ".h"} or path == owner:
            continue
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if raw_irq.search(line):
                errors.append(
                    f"[scheduler-runqueue-lock] {rel_to_root(path)}:{line_no}: "
                    "use rq_lock/rq_unlock (sched/core.h)"
                )
    return errors


def _line_skipped_for_isa_token_scan(line: str) -> bool:
    """Skip // comments and common block-comment markers (fewer false positives)."""
    s = line.lstrip()
    if not s:
        return True
    if s.startswith("//"):
        return True
    if s.startswith("/*") or s.startswith("*"):
        return True
    return False


def check_process_lifecycle_no_isa_conditionals():
    """Keep ISA selection in per-architecture backends, not process/*.c."""
    errors = []
    isa_re = re.compile(r"\b(?:__x86_64__|__amd64__|__aarch64__)\b")
    process_tree = ROOT / "kernel" / "process"

    for fpath in process_tree.glob("*.c"):
        lines = read_file_text(fpath).splitlines()
        for idx, line in enumerate(lines, 1):
            if _line_skipped_for_isa_token_scan(line):
                continue
            if isa_re.search(line):
                errors.append(
                    f"[process-no-isa-conditional] "
                    f"{rel_to_root(fpath)}:{idx}: move ISA policy to a "
                    f"selected backend: {line.strip()}"
                )
    return errors


def check_process_registry_ownership():
    """Only registry.c may mutate the global process-list head."""
    errors = []
    process_tree = ROOT / "kernel" / "process"
    owner = process_tree / "registry.c"
    mutation = re.compile(r"\bprocess_list\s*=")

    for path in process_tree.glob("*.c"):
        if path == owner:
            continue
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if mutation.search(line):
                errors.append(
                    f"[process-registry-owner] {rel_to_root(path)}:{line_no}: "
                    "use process_registry_insert/reset helpers"
                )
    return errors


def _cpp_directive_not_in_trailing_comment(line: str) -> Optional[str]:
    """
    Return the preprocessor token line, or None if the #if/elif is only in a comment.

    Avoids false positives like:  int x;  // #ifdef __aarch64__
    """
    s = line.rstrip("\n")
    # Strip // comments (whole-line or trailing).
    if "//" in s:
        before, _after = s.split("//", 1)
        s = before.rstrip()
    if not s.strip():
        return None
    if not re.match(r"^\s*#\s*(?:if|ifdef|ifndef|elif)\b", s):
        return None
    return s


def check_common_c_has_no_isa_conditionals():
    """Select ISA implementations by objects/facades, never inside common C."""
    errors = []
    isa_token = (
        r"(?:__x86_64__|__i386__|__amd64__|__aarch64__|ARCH_X86|"
        r"ARCH_ARM64|ARCH_ARM|CONFIG_ARCH_|IR0_ARCH_)"
    )
    isa_conditional = re.compile(
        rf"^\s*#\s*(?:if|ifdef|ifndef|elif)\b.*\b{isa_token}\b"
    )
    roots = (ROOT / "drivers", ROOT / "fs", ROOT / "interrupt",
             ROOT / "kernel", ROOT / "ktm", ROOT / "lib", ROOT / "mm",
             ROOT / "net", ROOT / "sched", ROOT / "smp", ROOT / "ipc")

    for root in roots:
        if not root.is_dir():
            continue
        for fpath in root.rglob("*.c"):
            for idx, line in enumerate(
                read_file_text(fpath).splitlines(), 1
            ):
                preproc = _cpp_directive_not_in_trailing_comment(line)
                if preproc is None:
                    continue
                if isa_conditional.search(preproc):
                    errors.append(
                        f"[common-c-no-isa-conditional] "
                        f"{rel_to_root(fpath)}:{idx}: select an object or "
                        f"facade backend (PC-only code: scripts/make/platform/pc.mk): "
                        f"{line.strip()}"
                    )
    return errors


def check_common_headers_have_only_data_model_isa_conditionals():
    """Only freestanding stdint/stddef may inspect compiler ISA width."""
    errors = []
    isa_conditional = re.compile(
        r"^\s*#\s*(?:if|ifdef|ifndef|elif).*"
        r"(?:__x86_64__|__i386__|__amd64__|__aarch64__|__arm__|"
        r"ARCH_X86|ARCH_ARM|CONFIG_ARCH_|IR0_ARCH_)"
    )
    # linux/sockios.h is verbatim Linux UAPI: its x32 test selects the
    # userspace SIOCGSTAMP value and never reaches kernel code paths.
    allowed = {"includes/stdint.h", "includes/stddef.h", "includes/uapi/linux/sockios.h"}

    for root in (ROOT / "includes", ROOT / "setup"):
        if not root.is_dir():
            continue
        for fpath in root.rglob("*.h"):
            rel = str(rel_to_root(fpath))
            for idx, line in enumerate(
                read_file_text(fpath).splitlines(), 1
            ):
                if isa_conditional.search(line) and rel not in allowed:
                    errors.append(
                        f"[common-header-no-isa-conditional] {rel}:{idx}: "
                        f"use a selected asm gateway: {line.strip()}"
                    )
    return errors


def check_arch_sources_need_no_compiler_isa_selection():
    """An arch/<isa> object is already selected; its C must not select again."""
    errors = []
    isa_conditional = re.compile(
        r"^\s*#\s*(?:if|ifdef|ifndef|elif).*"
        r"(?:__x86_64__|__i386__|__amd64__|__aarch64__|__arm__|"
        r"_M_X64|_M_IX86|_M_ARM)"
    )
    arch_tree = ROOT / "arch"

    for fpath in arch_tree.rglob("*.c"):
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(), 1
        ):
            if isa_conditional.search(line):
                errors.append(
                    f"[arch-source-no-compiler-isa-conditional] "
                    f"{rel_to_root(fpath)}:{idx}: the build already "
                    f"selected this ISA object: {line.strip()}"
                )
    return errors


def check_process_lifecycle_uses_semantic_return_state():
    """Keep x86 segment-state inspection out of common process lifecycle code."""
    errors = []
    forbidden = re.compile(
        r"\btask_(?:get|set)_(?:cs|ss)\s*\(|process_ctx_invariant\.h"
    )
    process_tree = ROOT / "kernel" / "process"

    for fpath in process_tree.glob("*.c"):
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(), 1
        ):
            if forbidden.search(line) and not line.lstrip().startswith("/*"):
                errors.append(
                    f"[process-return-state-neutral] {rel_to_root(fpath)}:"
                    f"{idx}: use task return-state/MM facades: {line.strip()}"
                )
    return errors


def check_portable_task_api_has_no_x86_register_aliases():
    """Keep public task accessors semantic instead of naming x86 registers."""
    errors = []
    forbidden = re.compile(r"\btask_(?:get|set)_(?:rdi|rsi|rdx)\s*\(")
    roots = (ROOT / "includes" / "ir0", ROOT / "kernel", ROOT / "lib",
             ROOT / "mm", ROOT / "net", ROOT / "sched", ROOT / "smp",
             ROOT / "ipc")

    for root in roots:
        if not root.is_dir():
            continue
        for fpath in iter_c_files(root):
            for idx, line in enumerate(
                read_file_text(fpath).splitlines(), 1
            ):
                if forbidden.search(line):
                    errors.append(
                        f"[task-api-semantic-registers] {rel_to_root(fpath)}:"
                        f"{idx}: use task arg/retval/IP/SP accessors: {line.strip()}"
                    )
    return errors


def platform_object_sources(platform):
    """C sources of the objects scripts/make/platform/<platform>.mk lists."""
    path = ROOT / "scripts" / "make" / "platform" / f"{platform}.mk"
    if not path.is_file():
        return set()
    return {m.group(1) + ".c"
            for m in re.finditer(r"([\w./-]+)\.o\b", read_file_text(path))}


# x86 headers a PC platform driver may include: port I/O, MSRs, CPUID.
PC_PLATFORM_ASM_INCLUDE = re.compile(
    r'^\s*#\s*include\s*<asm/(?:io|msr|processor|debugreg)\.h>')


def check_portable_code_has_no_private_asm_headers():
    """Allow only reviewed layout-selection gateways into private asm headers.

    The PC platform's device drivers (scripts/make/platform/pc.mk) build
    only on x86-64 and may include the x86 port-I/O, MSR and CPUID headers.
    """
    errors = []
    private_include = re.compile(r'^\s*#\s*include\s*[<"]asm/')
    pc_sources = platform_object_sources("pc")
    selection_gateways = {
        "includes/ir0/task.h": "#include <asm/task_context.h>",
        "includes/ir0/syscall_frame.h": "#include <asm/syscall_frame.h>",
        "includes/ir0/signals.h": "#include <asm/sigcontext.h>",
        "includes/ir0/arch_task.h": "#include <asm/task_access.h>",
        "includes/ir0/abi/vdso_contract.h": "#include <asm/vdso_contract.h>",
        "includes/ir0/abi/signal_contract.h":
            "#include <asm/signal_contract.h>",
        "includes/ir0/cacheflush.h": "#include <asm/cacheflush.h>",
        "includes/ir0/atomic.h": "#include <asm/atomic.h>",
        "includes/ir0/stat_user.h": "#include <asm/stat.h>",
        "includes/ir0/open_flags.h": "#include <asm/fcntl.h>",
        "includes/ir0/kexec.h": "#include <asm/kexec.h>",
    }
    roots = (ROOT / "includes" / "ir0", ROOT / "kernel", ROOT / "lib",
             ROOT / "mm", ROOT / "net", ROOT / "sched", ROOT / "smp",
             ROOT / "fs",
             ROOT / "drivers", ROOT / "ipc")

    for root in roots:
        if not root.is_dir():
            continue
        for fpath in iter_c_files(root):
            for idx, line in enumerate(
                read_file_text(fpath).splitlines(), 1
            ):
                if private_include.search(line):
                    rel = str(rel_to_root(fpath))
                    if selection_gateways.get(rel) == line.strip():
                        continue
                    if rel in pc_sources and PC_PLATFORM_ASM_INCLUDE.search(line):
                        continue
                    errors.append(
                        f"[portable-no-private-asm-header] "
                        f"{rel}:{idx}: use an ir0 facade: "
                        f"{line.strip()}"
                    )
    return errors


def check_common_paging_uses_neutral_roots():
    """Keep ISA register/table vocabulary out of the common paging contract."""
    errors = []
    legacy = re.compile(
        r"\b(?:CR3|cr3|PML4|pml4|PDPT|pdpt|PAGE_NX|CONFIG_ARCH_X86_64)\b"
    )

    # Every unit of the split paging core (paging.c, paging_*.c, headers).
    for fpath in sorted((ROOT / "mm").rglob("paging*.[ch]")):
        relative = fpath.relative_to(ROOT).as_posix()
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(), 1
        ):
            if legacy.search(line):
                errors.append(
                    f"[paging-neutral-root] {relative}:{idx}: use address-space "
                    f"root/translation-level vocabulary: {line.strip()}"
                )
    return errors


def check_common_address_space_contracts():
    """Prevent x86 page-table names from leaking back into common MM users."""
    errors = []
    legacy = re.compile(r"\b(?:CR3|cr3|PML4|pml4|PDPT|pdpt)\b")
    contracts = (
        "includes/ir0/memfd.h",
        "includes/ir0/ktm/user_canary.h",
        "mm/shmem/memfd.c",
        "ipc/sysv_shm.c",
        "kernel/exec/vdso.c",
        "mm/mm_struct.c",
        "ktm/user_canary.c",
    )

    for relative in contracts:
        fpath = ROOT / relative
        for idx, line in enumerate(
            read_file_text(fpath).splitlines(), 1
        ):
            if legacy.search(line):
                errors.append(
                    f"[address-space-neutral] {relative}:{idx}: use address-space "
                    f"root/translation-table vocabulary: {line.strip()}"
                )
    return errors


def check_common_elf_uses_isa_policy():
    """Keep ELF machine and relocation numbers in build-selected backends."""
    errors = []
    forbidden = re.compile(
        r"\b(?:ARCH_ELF_MACHINE|IR0_R_X86_64_[A-Z0-9_]+|R_X86_64_[A-Z0-9_]+|"
        r"R_AARCH64_[A-Z0-9_]+)\b"
    )
    paths = [ROOT / "kernel", ROOT / "lib", ROOT / "mm", ROOT / "fs",
             ROOT / "net", ROOT / "sched", ROOT / "smp", ROOT / "ipc"]

    for base in paths:
        for path in iter_c_files(base):
            for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if forbidden.search(line):
                    rel = rel_to_root(path)
                    errors.append(
                        f"[elf-isa-policy] {rel}:{line_no}: use arch_elf facade"
                    )
    return errors


def check_common_mm_uses_user_va_policy():
    """Keep ISA canonical user-VA bounds in MM backends."""
    errors = []
    forbidden = re.compile(r"0x(?:00400000|00007FFFFFFFFFFF)(?:ULL|UL|LL|L)?\b")

    for base in (ROOT / "kernel", ROOT / "mm"):
        for path in iter_c_files(base):
            for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if forbidden.search(line):
                    rel = rel_to_root(path)
                    errors.append(
                        f"[user-va-policy] {rel}:{line_no}: use arch_mm facade"
                    )
    return errors


def check_common_syscall_policy_uses_semantic_ids():
    """Keep native syscall numbers inside the build-selected ISA decoders."""
    errors = []
    targets = (
        ROOT / "kernel" / "syscalls" / "syscall_dispatch.c",
    )
    forbidden = re.compile(r"\b__NR_[A-Za-z0-9_]+|syscall_linux\.h")

    for path in targets:
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if forbidden.search(line):
                rel = rel_to_root(path)
                errors.append(
                    f"[syscall-semantic-id] {rel}:{line_no}: use syscall_id facade"
                )
    return errors


def check_sysv_shm_is_network_independent():
    """Keep SysV IPC registration independent from optional networking."""
    path = ROOT / "kernel" / "syscalls" / "syscall_dispatch.c"
    errors = []
    network_depths = []
    depth = 0

    for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        stripped = line.strip()
        if stripped.startswith("#if"):
            depth += 1
            if "CONFIG_ENABLE_NETWORKING" in stripped:
                network_depths.append(depth)
        elif stripped.startswith("#endif"):
            if network_depths and network_depths[-1] == depth:
                network_depths.pop()
            depth = max(0, depth - 1)

        if ("syscall_semantic_table[IR0_SYSCALL_SHM" in stripped and
                network_depths):
            errors.append(
                f"[sysv-shm-config] {rel_to_root(path)}:{line_no}: "
                "SysV SHM must not depend on networking"
            )
    return errors


def _parse_devfs_pty_id_block():
    """Return (pts0, pty_max) from includes/ir0/pty_devfs.h."""
    pty_h = ROOT / "includes" / "ir0" / "pty_devfs.h"
    pts0, pty_max = 48, 8
    if not pty_h.is_file():
        return pts0, pty_max
    text = pty_h.read_text(encoding="utf-8", errors="replace")
    m0 = re.search(r"#define\s+DEVFS_PTS0_DEVICE_ID\s+(\d+)u?", text)
    m1 = re.search(r"#define\s+DEVFS_PTY_MAX\s+(\d+)", text)
    if m0:
        pts0 = int(m0.group(1))
    if m1:
        pty_max = int(m1.group(1))
    return pts0, pty_max


def check_devfs_unique_device_ids():
    """
    devfs_find_node_by_id() returns the first registration match (Linux uses
    unique dev_t). Duplicate device_id values break fd routing (PTY incident).
    """
    errors = []
    devfs_paths = (ROOT / "fs" / "devfs.c", ROOT / "fs" / "devfs_pty.c")
    pts0, pty_max = _parse_devfs_pty_id_block()
    pty_end = pts0 + pty_max
    disk_base = 20
    disk_whole_count = 4

    owners = {}
    id_re = re.compile(r"\.device_id\s*=\s*(\d+)\b")
    name_re = re.compile(r'\.name\s*=\s*"([^"]+)"')

    def claim(dev_id, label, line_no, path):
        prev = owners.get(dev_id)
        if prev and prev[0] != label:
            rel_prev = rel_to_root(prev[2])
            rel_new = rel_to_root(path)
            errors.append(
                f"[devfs-unique-device-id] id={dev_id} used by {prev[0]}:{prev[1]} "
                f"({rel_prev}) and {label}:{line_no} ({rel_new})"
            )
            return
        owners[dev_id] = (label, line_no, path)

    reserved = []
    for i in range(pty_max):
        reserved.append((pts0 + i, pts0 + i + 1, f"pts/{i}"))
    for d in range(disk_whole_count):
        base = disk_base + d
        reserved.append((base, base + 1, f"disk-whole-{d}"))

    def in_reserved(dev_id):
        for start, end, _label in reserved:
            if start <= dev_id < end:
                return _label
        return None

    for devfs_path in devfs_paths:
        if not devfs_path.is_file():
            continue
        try:
            lines = devfs_path.read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError as exc:
            errors.append(f"[read-error] {devfs_path}: {exc}")
            return errors

        for idx, line in enumerate(lines, start=1):
            m_id = id_re.search(line)
            if not m_id:
                continue
            dev_id = int(m_id.group(1))
            m_name = name_re.search(line)
            label = m_name.group(1) if m_name else f"line-{idx}"
            slot = in_reserved(dev_id)
            if slot and not label.startswith("pts/") and "DEVFS_PTS0" not in line:
                rel = rel_to_root(devfs_path)
                errors.append(
                    f"[devfs-unique-device-id] static id={dev_id} ({label}:{idx} {rel}) "
                    f"falls in reserved {slot} block"
                )
            claim(dev_id, label, idx, devfs_path)

    for start, end, label in reserved:
        for dev_id in range(start, end):
            claim(dev_id, label, f"reserved-{dev_id}", devfs_paths[0])

    return errors


def check_devfs_no_find_by_id_outside_devfs():
    """
    fd_table stores dev_node at open; re-resolving by device_id in syscalls was
    a first-match footgun (PTY vs evdev collision). Allow only fs/devfs.c,
    fs/devfs_pty.c, and
    the documented fallback in includes/ir0/devfs.h.
    """
    errors = []
    allow_files = {
        ROOT / "fs" / "devfs.c",
        ROOT / "fs" / "devfs_pty.c",
        ROOT / "includes" / "ir0" / "devfs.h",
    }
    scan_roots = [ROOT / "kernel", ROOT / "fs", ROOT / "mm", ROOT / "net"]
    needle = "devfs_find_node_by_id"

    for root in scan_roots:
        if not root.is_dir():
            continue
        for path in root.rglob("*"):
            if not path.is_file() or path.suffix not in (".c", ".h"):
                continue
            if path in allow_files:
                continue
            try:
                text = path.read_text(encoding="utf-8", errors="replace")
            except OSError as exc:
                errors.append(f"[read-error] {path}: {exc}")
                continue
            if needle in text:
                rel = rel_to_root(path)
                errors.append(
                    f"[devfs-find-by-id] {needle} in portable path {rel} "
                    f"(use fd_entry_devfs_node / dev_node at open)"
                )
    return errors


def check_ktm_mock_boundaries():
    """Keep fake hardware test-only and outside production driver trees."""
    errors = []
    mock_root = ROOT / "ktm" / "mocks"
    product_configs = [
        ROOT / "setup" / "defconfig",
        ROOT / "setup" / "configs" / "userspace-ext2-root.defconfig",
    ]
    forbidden_roots = [
        ROOT / "arch",
        ROOT / "drivers",
        ROOT / "fs",
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "ipc",
    ]

    legacy = ROOT / "ktm" / "backends"
    if legacy.exists() and any(iter_c_files(legacy)):
        errors.append("[ktm-mock-layout] ktm/backends must not contain test doubles")

    for cfg in product_configs:
        if not cfg.exists():
            continue
        for line_no, line in enumerate(cfg.read_text(errors="replace").splitlines(), 1):
            if re.match(r"CONFIG_KTM_.*(?:FAKE|MOCK).*=y$", line.strip()):
                rel = cfg.relative_to(ROOT)
                errors.append(
                    f"[ktm-mock-product-config] {rel}:{line_no}: {line.strip()}"
                )

    # A developer .config may select mocks, but only on top of KTM. Without
    # KTM the mock object is not linked and the build would silently keep the
    # production backend, which is exactly the ambiguity the mocks avoid.
    local_config = ROOT / ".config"
    if local_config.exists():
        ktm_enabled = False
        enabled_mocks = []
        for line_no, line in enumerate(
            local_config.read_text(errors="replace").splitlines(), 1
        ):
            stripped = line.strip()
            if stripped == "CONFIG_KTM=y":
                ktm_enabled = True
            elif re.match(r"CONFIG_KTM_.*(?:FAKE|MOCK)=y$", stripped):
                enabled_mocks.append((line_no, stripped))
        if enabled_mocks and not ktm_enabled:
            for line_no, symbol in enabled_mocks:
                errors.append(
                    f"[ktm-mock-requires-ktm] .config:{line_no}: {symbol}"
                )

    mock_include = re.compile(r'^\s*#\s*include\s*[<\"]ktm_mock_')
    for base in forbidden_roots:
        if not base.is_dir():
            continue
        for path in iter_c_files(base):
            for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                if mock_include.search(line):
                    rel = rel_to_root(path)
                    errors.append(
                        f"[ktm-mock-production-include] {rel}:{line_no}: {line.strip()}"
                    )

    if mock_root.exists():
        for path in iter_c_files(mock_root):
            text = path.read_text(errors="replace")
            if "ir0_boot_smoke" in text or "klog_smoke" in text:
                rel = rel_to_root(path)
                errors.append(
                    f"[ktm-mock-self-report] {rel}: harness must observe facade results"
                )
    return errors


def check_hardware_backends_do_not_self_test_log():
    """Hardware backends return status; boot/KTM harnesses own test markers."""
    errors = []
    paths = [
        ROOT / "drivers" / "virtio" / "virtio_mmio.c",
        ROOT / "arch" / "arm64" / "sources" / "virtio_blk_early.c",
    ]
    forbidden = ("ir0_boot_smoke", "klog_smoke", "pl011_puts")

    for path in paths:
        if not path.exists():
            continue
        for line_no, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if any(token in line for token in forbidden):
                rel = rel_to_root(path)
                errors.append(
                    f"[hardware-self-test-log] {rel}:{line_no}: {line.strip()}"
                )
    return errors


# Assembly is a raw mechanism boundary.  Keep its C dependencies deliberately
# small: a new extern here needs an explicit architecture review instead of
# silently pulling process/MM/scheduler policy into an ISA implementation.
ASM_C_EXTERN_ALLOW = {
    "arch/x86-64/asm/boot/boot_x64.asm": {"kmain"},
    "arch/x86-64/asm/entry/syscall_64.asm": {
        "syscall_dispatch",
        "process_capture_syscall_frame_at_entry",
        "process_syscall_restore_exit_regs",
        "tls_restore_current",
    },
    "arch/x86-64/asm/context/switch_x64.asm": {
        "fork_ret_emit_pre_return",
        "fork_ret_pre_regs",
        "fork_flow_set_tf",
        "fork_restore_audit",
        "_etext",
        "switch_report_bad_ret",
        "process_after_task_save",
    },
}
ASM_EXTERN_RE = re.compile(r"^\s*extern\s+([A-Za-z_][A-Za-z0-9_]*)\b", re.M)


def check_asm_c_boundary():
    """Reject unreviewed C entry points from the x86 assembly boundary."""
    errors = []
    for rel, allowed in ASM_C_EXTERN_ALLOW.items():
        path = ROOT / rel
        if not path.is_file():
            errors.append(f"[asm-c-boundary] missing reviewed assembly file: {rel}")
            continue
        for symbol in ASM_EXTERN_RE.findall(path.read_text(errors="replace")):
            if symbol not in allowed:
                errors.append(
                    f"[asm-c-boundary] {rel}: unreviewed C boundary symbol '{symbol}'"
                )
    return errors


COMMON_MM_LOCAL_TLB_RE = re.compile(r"\btlb_invalidate_(?:page|all)\s*\(")

ERRNO_DEFINE_RE = re.compile(
    r"^\s*#\s*define\s+(E[A-Z0-9_]+)\b", re.MULTILINE
)

# Retired duplicates of the <ir0/kmem.h> API and the mm-private heap backend.
KMEM_RETIRED_RE = re.compile(
    r"\b(kmalloc_try|kmalloc_aligned_try|krealloc|all_realloc|"
    r"__kmalloc_checked|__kmalloc_aligned_checked|__krealloc_checked)\b"
)
HEAP_BACKEND_INCLUDE_RE = re.compile(r'#\s*include\s*[<"](?:mm/(?:alloc/)?)?allocator\.h[>"]')


def check_errno_has_single_catalog():
    """Reject subsystem-local copies of canonical Linux errno names."""
    errors = []
    catalog = ROOT / "includes" / "errno.h"
    canonical_names = set(ERRNO_DEFINE_RE.findall(catalog.read_text(errors="replace")))

    for base in (
        ROOT / "arch",
        ROOT / "drivers",
        ROOT / "fs",
        ROOT / "includes",
        ROOT / "interrupt",
        ROOT / "ipc",
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "mm",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "tests",
    ):
        for path in iter_c_files(base):
            if path == catalog:
                continue
            for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
                match = ERRNO_DEFINE_RE.match(line)
                if match and match.group(1) in canonical_names:
                    errors.append(
                        f"[errno-local-definition] {rel_to_root(path)}:{lineno}: "
                        f"{match.group(1)} belongs in includes/errno.h"
                    )
    return errors


RETIRED_UACCESS_RE = re.compile(
    r"\b(access_ok_mapped|validate_userspace_buffer|validate_userspace_string|"
    r"process_validate_userspace_buffer|is_user_address(_checked)?|"
    r"copy_from_user_cstring|copy_(to|from)_user_in_mm|zero_user_mm|"
    r"(copy_to|copy_from|zero)_user_region_in_directory|VALIDATE_BUFFER|"
    r"put_user|get_user)\b"
)


# The user-window arithmetic behind access_ok() and the copies: mm-private
# (mm/uaccess/user_va.h). Outside mm/ a pointer is checked with access_ok() and a
# mapping range with mm_user_range_mappable().
MM_PRIVATE_UACCESS_RE = re.compile(r"\b(mm_user_va_ok)\b")


def uaccess_violations(rel: str, text: str) -> list[tuple[int, str]]:
    """Return (line, message) for each user-pointer API rule *text* breaks."""
    out = []
    in_mm = rel.startswith("mm/")
    for lineno, line in enumerate(text.splitlines(), 1):
        match = RETIRED_UACCESS_RE.search(line)
        if match:
            out.append((lineno, f"{match.group(1)} is retired; use access_ok / "
                        f"copy_*_user / strncpy_from_user / copy_*_user_mm"))
        match = MM_PRIVATE_UACCESS_RE.search(line)
        if match and not in_mm:
            out.append((lineno, f"{match.group(1)} is mm-private; use "
                        f"access_ok (pointer) or mm_user_range_mappable "
                        f"(mapping range)"))
    return out


def check_uaccess_single_api():
    """Keep one user-pointer API: access_ok + copy_*_user (+ _mm, strncpy)."""
    errors = []
    for base in (
        ROOT / "arch",
        ROOT / "drivers",
        ROOT / "fs",
        ROOT / "includes",
        ROOT / "ipc",
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "ktm",
        ROOT / "mm",
        ROOT / "net",
        ROOT / "sched",
    ):
        for path in iter_c_files(base):
            rel = str(rel_to_root(path)).replace("\\", "/")
            for lineno, msg in uaccess_violations(rel, read_file_text(path)):
                errors.append(f"[uaccess-single-api] {rel}:{lineno}: {msg}")
    return errors


def _uaccess_self_test() -> list[str]:
    """Prove the scanner flags injected uses and allows mm/ its helper."""
    errors = []
    sample = (
        "static int ok(void *p) { return access_ok(p, 4); }\n"
        "int bad(uintptr_t a) { return mm_user_va_ok(a, 1); }\n"
        "int old(void *p) { return access_ok_mapped(p, 1); }\n"
        "void scalar(int x, int *p) { put_user(x, p); }\n"
        "void scalar_in(int x, int *p) { get_user(x, p); }\n"
    )
    got = [line for line, _ in uaccess_violations("kernel/x.c", sample)]
    if got != [2, 3, 4, 5]:
        errors.append(f"[uaccess-single-api] self-test: kernel/x.c flagged "
                      f"lines {got}, expected [2, 3, 4, 5]")
    got = [line for line, _ in uaccess_violations("mm/x.c", sample)]
    if got != [3, 4, 5]:
        errors.append(f"[uaccess-single-api] self-test: mm/x.c flagged "
                      f"lines {got}, expected [3, 4, 5]")
    return errors


def check_common_mm_uses_tlb_sync():
    """Keep root ownership policy out of ISA-local TLB mechanisms."""
    errors = []
    for path in sorted((ROOT / "mm").rglob("*.c")):
        if path.name == "tlb_sync.c":
            continue
        for lineno, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
            if COMMON_MM_LOCAL_TLB_RE.search(line):
                errors.append(
                    f"[mm-tlb-sync] {rel_to_root(path)}:{lineno}: "
                    "use mm_tlb_sync_page/all with the affected address-space root"
                )
    return errors


def check_kmem_single_api():
    """One kernel heap API (<ir0/kmem.h>); the heap backend stays in mm/."""
    errors = []
    backend_users = {
        Path("kernel/test/test_allocator.c"),
    }
    for base in (
        ROOT / "arch",
        ROOT / "drivers",
        ROOT / "fs",
        ROOT / "includes",
        ROOT / "ipc",
        ROOT / "kernel",
        ROOT / "lib",
        ROOT / "smp",
        ROOT / "ktm",
        ROOT / "mm",
        ROOT / "net",
        ROOT / "sched",
        ROOT / "sound",
        ROOT / "tests",
    ):
        for path in iter_c_files(base):
            rel = rel_to_root(path)
            for lineno, line in enumerate(read_file_text(path).splitlines(), 1):
                match = KMEM_RETIRED_RE.search(line)
                if match:
                    errors.append(
                        f"[kmem-single-api] {rel}:{lineno}: {match.group(1)} "
                        "was retired; use kmalloc/kcalloc/kmalloc_aligned/kfree"
                    )
                if (HEAP_BACKEND_INCLUDE_RE.search(line) and
                        rel.parts[0] != "mm" and rel not in backend_users):
                    errors.append(
                        f"[kmem-single-api] {rel}:{lineno}: the heap backend "
                        "header is mm-private; include <ir0/kmem.h>"
                    )
    return errors


# ISA register names in portable identifiers (rsp, rip, rax, cr3, esr, ...).
# Portable code speaks ip / sp / retval / root; register names live in arch/
# (the frame, context and sigcontext layouts in arch/<isa>/include/asm/).
# Existing offenders are frozen
# in REGISTER_NAME_BASELINE as "<path> <identifier>" pairs. The ledger only
# shrinks: a new pair fails, and so does a pair whose identifier is gone, so a
# branch that fixes an offender deletes its line in the same commit.
REGISTER_NAME_BASELINE = ROOT / "scripts" / "isa_register_name_baseline.txt"
REGISTER_NAME_TREES = ("block", "fs", "includes/ir0", "ipc", "kernel", "ktm",
                       "lib", "mm", "net", "sched", "smp")
REGISTER_NAME_PARTS = frozenset(
    {
        "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rbp", "rsp", "rip",
        "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
        "rflags", "eflags", "cr0", "cr2", "cr3", "cr4", "pml4",
        "iretq", "sysret", "esr", "elr", "spsr", "ttbr0", "ttbr1", "x30",
    }
)
# "cs"/"ss" also abbreviate sock_stream etc.; they name a segment register
# only next to a CPU-context word (task_get_cs, fault_cs, frame_ss).
REGISTER_SEGMENT_PARTS = frozenset({"cs", "ss"})
REGISTER_SEGMENT_CONTEXT = frozenset({"task", "fault", "frame"})
_REGISTER_IDENT_RE = re.compile(r"\b[A-Za-z_][A-Za-z0-9_]*\b")
_C_COMMENT_RE = re.compile(r"/\*.*?\*/", re.S)
_C_LINE_COMMENT_RE = re.compile(r"//[^\n]*")
_C_STRING_RE = re.compile(r'"(?:\\.|[^"\\\n])*"' + r"|'(?:\\.|[^'\\\n])*'")


def _register_name_identifiers(text: str) -> dict[str, int]:
    """Map each register-named identifier in C *text* to its first line."""
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    text = _C_STRING_RE.sub('""', text)
    found: dict[str, int] = {}
    for lineno, line in enumerate(text.splitlines(), 1):
        if line.lstrip().startswith("#include"):
            continue
        for match in _REGISTER_IDENT_RE.finditer(line):
            ident = match.group(0)
            parts = ident.lower().split("_")
            segment = (any(p in REGISTER_SEGMENT_PARTS for p in parts) and
                       any(p in REGISTER_SEGMENT_CONTEXT for p in parts))
            if segment or any(p in REGISTER_NAME_PARTS for p in parts):
                found.setdefault(ident, lineno)
    return found


def _load_register_name_baseline() -> set[tuple[str, str]]:
    entries: set[tuple[str, str]] = set()
    if not REGISTER_NAME_BASELINE.exists():
        return entries
    for raw in REGISTER_NAME_BASELINE.read_text(encoding="utf-8").splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        parts = line.split()
        if len(parts) != 2:
            raise ValueError(f"bad register-name baseline line: {raw!r}")
        entries.add((parts[0], parts[1]))
    return entries


def register_name_violations() -> dict[tuple[str, str], int]:
    """Return {(path, identifier): line} for the portable trees."""
    violations: dict[tuple[str, str], int] = {}
    for tree in REGISTER_NAME_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for path in iter_c_files(base):
            rel = str(rel_to_root(path)).replace("\\", "/")
            if "/mocks/arm64/" in rel or "/mocks/x86" in rel:
                continue
            for ident, lineno in _register_name_identifiers(
                    read_file_text(path)).items():
                violations[(rel, ident)] = lineno
    return violations


def register_name_errors(violations: dict[tuple[str, str], int],
                         baseline: set[tuple[str, str]]) -> list[str]:
    """Report pairs missing from the ledger and ledger lines that are gone."""
    errors = []
    for (rel, ident), lineno in sorted(violations.items()):
        if (rel, ident) in baseline:
            continue
        errors.append(
            f"[portable-no-register-names] {rel}:{lineno}: {ident} names an "
            "ISA register; use ip/sp/retval/arg/root accessors and keep "
            "registers in arch/"
        )
    for rel, ident in sorted(baseline - set(violations)):
        errors.append(
            f"[portable-no-register-names] stale ledger entry '{rel} {ident}': "
            f"the identifier is gone; delete the line from "
            f"{rel_to_root(REGISTER_NAME_BASELINE)}"
        )
    return errors


def _register_name_self_test() -> list[str]:
    """Prove the scanner flags an injected name and honours comments, and
    that the ledger rejects both a new pair and a stale line."""
    errors = []
    sample = (
        "/* rip in a comment is fine */\n"
        "static int ok(const char *s) { return s[0] == 'r'; }\n"
        "uint64_t user_ip; const char *t = \"rsp=%llx\";\n"
        "uint64_t saved_rsp;\n"
        "static inline uint16_t task_get_cs(void);\n"
        "int cs; int css_count; int ss_state;\n"
    )
    found = _register_name_identifiers(sample)
    if set(found) != {"saved_rsp", "task_get_cs"}:
        errors.append(
            "[portable-no-register-names] self-test: expected "
            f"saved_rsp and task_get_cs, scanner found {sorted(found)}"
        )
    if found.get("saved_rsp") != 4:
        errors.append(
            "[portable-no-register-names] self-test: wrong line for saved_rsp"
        )
    live = {("fs/a.c", "saved_rsp"): 4}
    cases = (
        ("clean", live, {("fs/a.c", "saved_rsp")}, []),
        ("new pair", {**live, ("fs/b.c", "frame_rip"): 7},
         {("fs/a.c", "saved_rsp")}, ["fs/b.c:7: frame_rip"]),
        ("stale line", live,
         {("fs/a.c", "saved_rsp"), ("fs/a.c", "old_rax")},
         ["stale ledger entry 'fs/a.c old_rax'"]),
    )
    for name, violations, baseline, expected in cases:
        got = register_name_errors(violations, baseline)
        if len(got) != len(expected) or not all(
                want in err for want, err in zip(expected, got)):
            errors.append(
                f"[portable-no-register-names] self-test {name}: expected "
                f"{expected}, got {got}"
            )
    return errors


# Terminal halt: the only callers of cpu_halt() (Linux stop_this_cpu users).
TERMINAL_HALT_CALLERS = frozenset((
    "kernel/panic/oops.c",
    "kernel/kexec/kexec.c",
    "kernel/power/power_manag.c",
))
TERMINAL_HALT_HEADER = "includes/ir0/oops_port.h"
RETIRED_WAIT_RE = re.compile(
    r"\b(cpu_relax|kernel_idle_poll|kernel_idle_poll_nosched|"
    r"process_idle_bind|process_idle_services|pipe_wake_check|"
    r"sleep_wake_check|ir0_clock_wait_service_runqueue|"
    r"ir0_clock_wait_service_clockevents|clock_sleep_ticks)\b"
)
CPU_HALT_RE = re.compile(r"\bcpu_halt\s*\(")
BUSY_WAIT_TREES = ("arch", "drivers", "fs", "includes", "ipc", "kernel",
                   "ktm", "lib", "mm", "net", "sched", "smp", "sound", "cpp",
                   "tests")


def busy_wait_violations(rel: str, text: str) -> list[tuple[int, str]]:
    """Return (line, message) for each busy-wait rule *text* (at *rel*) breaks."""
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    text = _C_STRING_RE.sub('""', text)
    found = []
    in_arch = rel.startswith("arch/")
    for lineno, line in enumerate(text.splitlines(), 1):
        match = RETIRED_WAIT_RE.search(line)
        if match:
            found.append((lineno, f"{match.group(1)} is retired; sleep on a "
                          "wait queue, clock_sleep() or udelay()"))
        if not CPU_HALT_RE.search(line):
            continue
        is_decl = line.rstrip().endswith(";") and line.lstrip().startswith("void")
        if is_decl and rel != TERMINAL_HALT_HEADER:
            found.append((lineno, f"cpu_halt() is declared only in "
                          f"{TERMINAL_HALT_HEADER}"))
        elif not is_decl and not in_arch and rel not in TERMINAL_HALT_CALLERS:
            found.append((lineno, "cpu_halt() is the terminal halt of panic / "
                          "power-off / kexec; it is never a way to wait"))
    return found


def _busy_wait_self_test() -> list[str]:
    """Inject each violation; the scanner must report exactly those lines."""
    cases = {
        "fs/clean.c": ("/* cpu_relax() in a comment is fine */\n"
                       "static const char *m = \"cpu_halt()\";\n", []),
        "fs/relax.c": ("void f(void)\n{\n\twhile (busy)\n\t\tcpu_relax();\n}\n",
                       [4]),
        "net/halt.c": ("void f(void)\n{\n\tcpu_halt();\n}\n", [3]),
        "kernel/panic/oops.c": ("void f(void)\n{\n\tcpu_halt();\n}\n", []),
        "includes/ir0/cpu.h": ("void cpu_halt(void);\n", [1]),
        "kernel/boot.c": ("\tkernel_idle_poll();\n", [1]),
    }
    failures = []
    for rel, (text, expected) in cases.items():
        got = [line for line, _ in busy_wait_violations(rel, text)]
        if got != expected:
            failures.append(f"[no-busy-wait] self-test {rel}: expected "
                            f"lines {expected}, got {got}")
    return failures


def check_no_busy_wait_primitives():
    """Reject retired busy-wait names and cpu_halt() outside terminal paths."""
    errors = _busy_wait_self_test()
    for tree in BUSY_WAIT_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for path in iter_c_files(base):
            rel = str(rel_to_root(path)).replace("\\", "/")
            for lineno, msg in busy_wait_violations(rel, read_file_text(path)):
                errors.append(f"[no-busy-wait] {rel}:{lineno}: {msg}")
    return errors


# Atomic instructions and inline asm belong to arch/ or to the atomics
# backend. Portable code calls <ir0/atomic.h> (atomic_t, cmpxchg, bit locks,
# barrier()) and the <ir0/cpu.h> facades; it never spells __atomic_*,
# __sync_* or asm itself. RAW_ATOMIC_LEDGER lists reviewed exceptions as
# "<path>" lines; it starts empty and only shrinks.
RAW_ATOMIC_RE = re.compile(
    r"\b(__atomic_[a-z_]+|__sync_[a-z_]+)\s*\(|"
    r"\b(__asm__|__asm|asm)\b(?:\s+(?:volatile|__volatile__|goto|inline))*"
    r"\s*\(")
RAW_ATOMIC_TREES = ("block", "cpp", "drivers", "fs", "includes", "ipc",
                    "kernel", "ktm", "lib", "mm", "net", "sched", "smp",
                    "sound")
RAW_ATOMIC_BACKEND = "includes/asm-generic/atomic.h"
RAW_ATOMIC_LEDGER: frozenset[str] = frozenset()


def raw_atomic_violations(rel: str, text: str) -> list[tuple[int, str]]:
    """Return (line, name) for each raw atomic or asm use in *text*."""
    if rel.startswith("arch/") or rel == RAW_ATOMIC_BACKEND:
        return []
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    text = _C_STRING_RE.sub('""', text)
    found = []
    for lineno, line in enumerate(text.splitlines(), 1):
        match = RAW_ATOMIC_RE.search(line)
        if match:
            found.append((lineno, match.group(1) or match.group(2)))
    return found


def _raw_atomic_self_test() -> list[str]:
    """Inject each violation; the scanner must report exactly those lines."""
    cases = {
        "mm/clean.c": ("/* __atomic_load_n() and asm(\"\") in a comment */\n"
                       "static const char *m = \"__sync_lock_release(\";\n"
                       "#define X __ATOMIC_RELAXED\n"
                       "\tbarrier();\n\ttest_and_set_bit_lock(0, &f);\n",
                       []),
        "mm/flag.c": ("\tif (__atomic_test_and_set(&f, __ATOMIC_ACQUIRE))\n"
                      "\t\treturn;\n\t__atomic_clear(&f, 0);\n", [1, 3]),
        "net/lock.c": ("\twhile (__sync_lock_test_and_set(&b, 1))\n", [1]),
        "drivers/delay.c": ("\t__asm__ volatile(\"\" ::: \"memory\");\n"
                            "\tasm (\"pause\");\n", [1, 2]),
        "includes/ir0/x.h": ("register long x8 __asm__(\"x8\") = n;\n", [1]),
        "arch/x86-64/sources/cpu.c": ("\t__asm__ volatile(\"hlt\");\n", []),
        RAW_ATOMIC_BACKEND: ("\treturn __atomic_load_n(p, 0);\n", []),
    }
    failures = []
    for rel, (text, expected) in cases.items():
        got = [line for line, _ in raw_atomic_violations(rel, text)]
        if got != expected:
            failures.append(f"[no-raw-atomics] self-test {rel}: expected "
                            f"lines {expected}, got {got}")
    return failures


def check_no_raw_atomics():
    """Reject __atomic_*, __sync_* and inline asm outside arch/ and the
    atomics backend; stale ledger entries fail too."""
    errors = _raw_atomic_self_test()
    hit: set[str] = set()
    for tree in RAW_ATOMIC_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for path in iter_c_files(base):
            rel = str(rel_to_root(path)).replace("\\", "/")
            found = raw_atomic_violations(rel, read_file_text(path))
            if found and rel in RAW_ATOMIC_LEDGER:
                hit.add(rel)
                continue
            for lineno, name in found:
                errors.append(
                    f"[no-raw-atomics] {rel}:{lineno}: {name} outside arch/; "
                    "use <ir0/atomic.h> (atomic_t, cmpxchg, "
                    "test_and_set_bit_lock, barrier) or an arch facade")
    for rel in sorted(RAW_ATOMIC_LEDGER - hit):
        errors.append(f"[no-raw-atomics] {rel} is clean; drop it from "
                      "RAW_ATOMIC_LEDGER")
    return errors


# A header for one ISA lives in arch/<isa>/include/asm/ (or uapi/asm/) under
# a neutral name, and the $(ARCH) include directory selects it. includes/
# holds only portable APIs and the ISA-neutral uapi, so a path component
# there that names an ISA (foo_x86_64.h, bar_arm64.h, x86/) is a per-ISA
# file in the portable tree. No ledger: includes/ has none left.
ISA_NAMED_HEADER_TREE = "includes/"
ISA_NAME_RE = re.compile(
    r"(?:^|[_.-])(x86(?:[_-]?64)?|x64|amd64|i[3-6]86|arm64|aarch64|"
    r"riscv(?:32|64)?)(?:[_.-]|$)")


def isa_named_header(rel: str) -> str | None:
    """Return the ISA a header path under includes/ is named after."""
    if not rel.startswith(ISA_NAMED_HEADER_TREE):
        return None
    parts = rel[len(ISA_NAMED_HEADER_TREE):].split("/")
    parts[-1] = parts[-1].rsplit(".", 1)[0]
    for part in parts:
        match = ISA_NAME_RE.search(part)
        if match:
            return match.group(1)
    return None


def _isa_named_header_self_test() -> list[str]:
    cases = {
        "includes/ir0/arch_task_context_x86_64.h": "x86_64",
        "includes/ir0/sigcontext_arm64.h": "arm64",
        "includes/uapi/ir0/syscall_aarch64.h": "aarch64",
        "includes/ir0/x86-64/io.h": "x86-64",
        "includes/ir0/syscall_frame.h": None,
        "includes/ir0/arch_task.h": None,
        "includes/uapi/asm-generic/posix_types.h": None,
        "includes/ir0/alarm.h": None,
        "arch/x86-64/include/asm/task_context.h": None,
    }
    return [f"[isa-named-header] self-test {rel}: expected {want}, got {got}"
            for rel, want in cases.items()
            if (got := isa_named_header(rel)) != want]


def check_no_isa_named_headers():
    """Reject per-ISA headers under includes/; they belong in arch/<isa>/."""
    errors = _isa_named_header_self_test()
    base = ROOT / ISA_NAMED_HEADER_TREE
    for dirpath, dirnames, filenames in os.walk(base):
        dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIR_NAMES)
        for name in sorted(filenames):
            if not name.endswith((".h", ".inc")):
                continue
            rel = str(rel_to_root(Path(dirpath) / name)).replace("\\", "/")
            isa = isa_named_header(rel)
            if isa:
                errors.append(
                    f"[isa-named-header] {rel}: named after {isa}; move it "
                    f"to arch/<isa>/include/asm/ under a neutral name and "
                    f"include it as <asm/...>")
    return errors


# Userspace enters the kernel through libc syscall() or its own stubs
# (tests/selftests/ir0/nolibc.h, tests/conformance/rt.h, setup/pid1/*).
# The kernel, includes/ and host tooling never carry a syscall-trap
# wrapper: inline asm that issues syscall, sysenter, int $0x80 or svc #0 is
# rejected outside arch/ and the userspace trees below.
SYSCALL_STUB_ALLOWED = ("arch/", "userspace/", "setup/pid1/",
                        "tests/selftests/", "tests/conformance/",
                        "scripts/linux_abi/workloads/")
SYSCALL_STUB_SUFFIXES = (".c", ".h", ".cc", ".cpp", ".hpp")
_ASM_OPEN_RE = re.compile(
    r"\b(?:__asm__|__asm|asm)\b(?:\s+(?:volatile|__volatile__|goto|inline))*"
    r"\s*\(")
_C_STRING_BODY_RE = re.compile(r'"((?:\\.|[^"\\\n])*)"')
SYSCALL_TRAP_RE = re.compile(
    r"(?:^|[\s;])(syscall|sysenter|int\s+\$(?:0x80|128)|svc\s+#?(?:0x)?0)\b")


def syscall_stub_violations(rel: str, text: str) -> list[tuple[int, str]]:
    """Return (line, instruction) for each syscall-trap asm in *text*."""
    if rel.startswith(SYSCALL_STUB_ALLOWED):
        return []
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    found = []
    for match in _ASM_OPEN_RE.finditer(text):
        depth, pos = 1, match.end()
        while pos < len(text) and depth:
            char = text[pos]
            if char == '"':
                pos += 1
                while pos < len(text) and text[pos] not in '"\n':
                    pos += 2 if text[pos] == "\\" else 1
            elif char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
            pos += 1
        template = "\n".join(
            body.replace("\\n", "\n").replace("\\t", " ")
            for body in _C_STRING_BODY_RE.findall(text[match.end():pos]))
        trap = SYSCALL_TRAP_RE.search(template)
        if trap:
            found.append((text.count("\n", 0, match.start()) + 1,
                          trap.group(1)))
    return found


def _syscall_stub_self_test() -> list[str]:
    """Inject each violation; the scanner must report exactly those lines."""
    cases = {
        "kernel/x.c": ('\t__asm__ volatile("syscall" : "=a"(r) : "a"(n)'
                       ' : "rcx", "r11", "memory");\n', [1]),
        "includes/ir0/x.h": ("static inline long s(long n)\n{\n"
                             '\tregister long x8 __asm__("x8") = n;\n'
                             "\t__asm__ volatile(\n\t\t\"svc #0\"\n"
                             '\t\t: "+r"(x0) : "r"(x8) : "memory");\n', [4]),
        "arch/x86-64/include/asm/x.h": (
            '\tasm volatile("int $0x80" : "=a"(r) : "a"(n));\n', []),
        "lib/x.c": ('\tasm("movq %1, %%rax\\n\\tsyscall" : : "r"(n));\n',
                    [1]),
        "ktm/user_canary.c": ('\tcheck(root, sp, pid, "syscall");\n'
                              '\t/* asm volatile("syscall"); */\n'
                              '\t__asm__ volatile("hlt");\n'
                              '\tsyscall_entry();\n', []),
        "tests/selftests/ir0/nolibc.h": (
            '\t__asm__ volatile("syscall" : "=a"(r) : "a"(n));\n', []),
    }
    failures = []
    for rel, (text, expected) in cases.items():
        got = [line for line, _ in syscall_stub_violations(rel, text)]
        if got != expected:
            failures.append(f"[no-syscall-stubs] self-test {rel}: expected "
                            f"lines {expected}, got {got}")
    return failures


def check_no_syscall_stubs():
    """Reject syscall-trap inline asm outside arch/ and userspace trees."""
    errors = _syscall_stub_self_test()
    for dirpath, dirnames, filenames in os.walk(ROOT):
        rel_dir = os.path.relpath(dirpath, ROOT).replace("\\", "/")
        rel_dir = "" if rel_dir == "." else rel_dir + "/"
        dirnames[:] = sorted(
            d for d in dirnames if d not in SKIP_DIR_NAMES
            and not (rel_dir + d + "/").startswith(SYSCALL_STUB_ALLOWED))
        for name in sorted(filenames):
            if not name.endswith(SYSCALL_STUB_SUFFIXES):
                continue
            rel = rel_dir + name
            for lineno, insn in syscall_stub_violations(
                    rel, read_file_text(Path(dirpath) / name)):
                errors.append(
                    f"[no-syscall-stubs] {rel}:{lineno}: '{insn}' trap in "
                    "inline asm outside arch/ and userspace; userspace uses "
                    "libc syscall() or its own nolibc stubs")
    return errors


# The PC is a platform: its devices build from scripts/make/platform/pc.mk and
# generic code reaches them through facades the platform registers. A
# preprocessor switch for "legacy PC" hardware (the retired IR0_LEGACY_PC_IO
# or a successor) brings the branch back, and port I/O outside x86 code and
# the PC platform sources means generic code, or another ISA, touches PC
# hardware. No ledger: nothing is left.
LEGACY_PC_MACROS = ("IR0_LEGACY_PC_IO", "CONFIG_LEGACY_PC", "CONFIG_PLATFORM_PC",
                    "CONFIG_X86_PC", "CONFIG_HAS_IOPORT", "HAS_IOPORT")
LEGACY_PC_DIRECTIVE_RE = re.compile(
    r"^\s*#\s*(?:if|ifdef|ifndef|elif|define)\b.*\b("
    + "|".join(LEGACY_PC_MACROS) + r")\b")
PORT_IO_RE = re.compile(
    r"\b(inb|outb|inw|outw|inl|outl|insb|insw|insl|outsb|outsw|outsl|"
    r"io_wait)\s*\(")
PORT_IO_ALLOWED = ("arch/x86-64/",)
LEGACY_PC_TREES = SINGLE_DECL_TREES + ("cpp",)


def legacy_pc_violations(rel: str, text: str,
                         pc_sources: set[str]) -> list[tuple[int, str]]:
    """Return (line, name) for each legacy-PC switch or stray port I/O."""
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    text = _C_STRING_RE.sub('""', text)
    port_io_ok = rel.startswith(PORT_IO_ALLOWED) or rel in pc_sources
    found = []
    for lineno, line in enumerate(text.splitlines(), 1):
        match = LEGACY_PC_DIRECTIVE_RE.search(line)
        if match:
            found.append((lineno, match.group(1)))
            continue
        match = PORT_IO_RE.search(line)
        if match and not port_io_ok:
            found.append((lineno, match.group(1)))
    return found


def _legacy_pc_self_test() -> list[str]:
    """Inject each violation; the scanner must report exactly those lines."""
    pc_sources = {"drivers/storage/ata.c"}
    cases = {
        "drivers/char/vga.c": ("#if IR0_LEGACY_PC_IO\n\tvga[i] = c;\n"
                               "#elif !defined(CONFIG_HAS_IOPORT)\n#endif\n",
                               [1, 3]),
        "includes/ir0/x.h": ("#ifndef IR0_LEGACY_PC_IO\n"
                             "#define IR0_LEGACY_PC_IO 0\n#endif\n", [1, 2]),
        "drivers/net/x.c": ("\toutb(0x3f8, c);\n\tv = inl(PCI_DATA);\n"
                            "\tio_wait();\n", [1, 2, 3]),
        "arch/arm64/sources/interface.c": ("uint8_t inb(uint16_t port)\n{\n"
                                           "\treturn 0;\n}\n", [1]),
        "kernel/clean.c": ("/* #if IR0_LEGACY_PC_IO and outb(0x80, 0) */\n"
                           "// inb(0x64);\n"
                           "static const char *m = \"outw(\";\n"
                           "\tsb_inbox(1);\n\tkeyboard_inb_count++;\n", []),
        "arch/x86-64/sources/pic.c": ("\toutb(0x20, 0x11);\n", []),
        "drivers/storage/ata.c": ("\tstatus = inb(ATA_STATUS);\n", []),
    }
    failures = []
    for rel, (text, expected) in cases.items():
        got = [line for line, _ in legacy_pc_violations(rel, text, pc_sources)]
        if got != expected:
            failures.append(f"[legacy-pc-io] self-test {rel}: expected lines "
                            f"{expected}, got {got}")
    return failures


def check_legacy_pc_io():
    """Reject legacy-PC preprocessor switches anywhere and port I/O outside
    arch/x86-64/ and the PC platform sources."""
    errors = _legacy_pc_self_test()
    pc_sources = platform_object_sources("pc")
    for tree in LEGACY_PC_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for path in iter_c_files(base):
            rel = str(rel_to_root(path)).replace("\\", "/")
            for lineno, name in legacy_pc_violations(
                    rel, read_file_text(path), pc_sources):
                if name in LEGACY_PC_MACROS:
                    errors.append(
                        f"[legacy-pc-io] {rel}:{lineno}: {name} switches PC "
                        "hardware in the preprocessor; list the PC code in "
                        "scripts/make/platform/pc.mk and reach it through a "
                        "facade the platform registers")
                else:
                    errors.append(
                        f"[legacy-pc-io] {rel}:{lineno}: port I/O {name}() "
                        "outside arch/x86-64/ and the PC platform sources "
                        "(scripts/make/platform/pc.mk)")
    return errors


# Domain isolation. Some subsystems are a core plus independent members:
# filesystems in fs/<name>/ around the VFS core in fs/, later network
# protocols and driver classes. A member knows only the shared APIs (the
# core, permissions, memory, block class, common helpers), never another
# member: it includes no header of another member, calls or references no
# symbol another member defines, and tests no configuration symbol of
# another member. The core selects no member by configuration: the build
# registers members (scripts/make/fs/<name>.mk) and the core iterates the
# registry. A member is a subdirectory of the domain root; files directly in
# the root are the core. To isolate another domain, add a DomainSpec.
class DomainSpec:
    def __init__(self, name, root, member_config_re, fragments, core_extra,
                 not_members=()):
        self.name = name                # report tag suffix
        self.root = root                # "fs": members are fs/<m>/
        # One capture group: the member token of a member config symbol.
        self.member_config_re = re.compile(member_config_re)
        self.fragments = fragments      # "scripts/make/fs": <m>.mk each
        self.core_extra = core_extra    # more core files (the registry .mk)
        self.not_members = frozenset(not_members)


DOMAINS = (
    DomainSpec("fs", "fs", r"\bCONFIG_(?:ENABLE_)?FS_([A-Z0-9_]+)\b",
               "scripts/make/fs", ("scripts/make/fs.mk",)),
)

DOMAIN_SOURCE_SUFFIXES = (".c", ".h", ".S", ".mk")
_DOMAIN_INCLUDE_RE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]',
                                re.M)
# Top-level definitions a member exports: functions and initialized objects
# (a filesystem's struct vfs_fstype). static and extern lines are skipped.
_DOMAIN_FUNC_DEF_RE = re.compile(
    r"^(?P<head>[A-Za-z_][\w \t\*]*?)\b(?P<name>[A-Za-z_]\w*)[ \t]*"
    r"\((?P<args>[^;{}()]*(?:\([^;{}()]*\)[^;{}()]*)*)\)\s*\{", re.M)
_DOMAIN_OBJ_DEF_RE = re.compile(
    r"^(?P<head>[A-Za-z_][\w \t\*]*?)\b(?P<name>[A-Za-z_]\w*)[ \t]*"
    r"(?:\[[^\]\n]*\])?[ \t]*=", re.M)
_DOMAIN_IDENT_RE = re.compile(r"\b[A-Za-z_]\w*\b")


def _domain_token(member: str) -> str:
    return re.sub(r"\W", "_", member.upper())


def _domain_strip(rel: str, text: str) -> str:
    """Blank comments and strings, keeping line numbers."""
    if rel.endswith(".mk") or rel.endswith("Makefile"):
        return re.sub(r"#[^\n]*", "", text)
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    return _C_STRING_RE.sub('""', text)


def _domain_exports(rel: str, text: str) -> set[str]:
    if not rel.endswith(".c"):
        return set()
    names = set()
    for regex in (_DOMAIN_FUNC_DEF_RE, _DOMAIN_OBJ_DEF_RE):
        for match in regex.finditer(text):
            words = match.group("head").split()
            if not words or words[0] in ("static", "extern", "return",
                                         "else", "typedef"):
                continue
            names.add(match.group("name"))
    return names


def domain_violations(spec: DomainSpec,
                      files: dict[str, str]) -> list[tuple[str, int, str]]:
    """Return (path, line, message) for every isolation break in @files,
    a map from repository-relative path to text."""
    root = spec.root + "/"
    frag = spec.fragments + "/"
    member_of: dict[str, str] = {}
    core: list[str] = []
    for rel in files:
        if rel.startswith(root):
            parts = rel[len(root):].split("/")
            if len(parts) == 1:
                core.append(rel)
            elif parts[0] not in spec.not_members:
                member_of[rel] = parts[0]
        elif rel.startswith(frag) and rel.endswith(".mk"):
            member_of[rel] = rel[len(frag):-len(".mk")]
        elif rel in spec.core_extra:
            core.append(rel)
    members = sorted(set(member_of.values()))
    stripped = {rel: _domain_strip(rel, files[rel])
                for rel in list(member_of) + core}

    exported: dict[str, set[str]] = {m: set() for m in members}
    for rel, member in member_of.items():
        exported[member] |= _domain_exports(rel, stripped[rel])
    owner: dict[str, str] = {}
    for member, names in exported.items():
        for name in names:
            # A name two members both define is nobody's to borrow.
            owner[name] = "" if name in owner else member

    def other_include(path: str, member: Optional[str]) -> Optional[str]:
        for other in members:
            if other == member:
                continue
            if (path.startswith(f"{root}{other}/") or
                    path.startswith(f"{other}/") or
                    f"../{other}/" in path):
                return other
        return None

    found = []
    for rel in sorted(member_of):
        member = member_of[rel]
        token = _domain_token(member)
        raw = files[rel]
        for lineno, line in enumerate(stripped[rel].splitlines(), 1):
            for match in spec.member_config_re.finditer(line):
                if match.group(1) != token:
                    found.append((rel, lineno,
                                  f"{member} tests {match.group(0)}, the "
                                  "configuration of another member"))
            for match in _DOMAIN_IDENT_RE.finditer(line):
                other = owner.get(match.group(0))
                if other and other != member:
                    found.append((rel, lineno,
                                  f"{member} uses {match.group(0)}, defined "
                                  f"by {other}"))
        for match in _DOMAIN_INCLUDE_RE.finditer(raw):
            other = other_include(match.group(1), member)
            if other:
                lineno = raw.count("\n", 0, match.start()) + 1
                found.append((rel, lineno,
                              f"{member} includes {match.group(1)} of "
                              f"{other}"))
    for rel in sorted(core):
        for lineno, line in enumerate(stripped[rel].splitlines(), 1):
            for match in spec.member_config_re.finditer(line):
                found.append((rel, lineno,
                              f"the {spec.name} core tests "
                              f"{match.group(0)}; members are selected by "
                              f"{spec.fragments}/<member>.mk and registered"))
        raw = files[rel]
        for match in _DOMAIN_INCLUDE_RE.finditer(raw):
            other = other_include(match.group(1), None)
            if other:
                lineno = raw.count("\n", 0, match.start()) + 1
                found.append((rel, lineno,
                              f"the {spec.name} core includes "
                              f"{match.group(1)} of {other}"))
    found.sort(key=lambda item: (item[0], item[1], item[2]))
    return found


def _domain_files(spec: DomainSpec) -> dict[str, str]:
    files = {}
    for base in (ROOT / spec.root, ROOT / spec.fragments):
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIR_NAMES)
            for name in sorted(filenames):
                if name.endswith(DOMAIN_SOURCE_SUFFIXES):
                    path = Path(dirpath) / name
                    files[str(rel_to_root(path)).replace("\\", "/")] = \
                        read_file_text(path)
    for rel in spec.core_extra:
        if (ROOT / rel).is_file():
            files[rel] = read_file_text(ROOT / rel)
    return files


def _domain_isolation_self_test() -> list[str]:
    """Inject each kind of break; the scanner must report exactly those."""
    spec = DOMAINS[0]
    files = {
        "fs/minix/a.c": ("#include <fs/vfs.h>\n"
                         "#include <fs/tmpfs/tmpfs.h>\n"
                         "#include \"../fat16/fat16_disk.h\"\n"
                         "#include \"minix.h\"\n"),
        "fs/minix/b.c": ("int minix_read(void)\n{\n"
                         "\treturn tmpfs_lookup(1) + minix_local(2);\n}\n"
                         "/* tmpfs_lookup() in a comment is fine */\n"
                         "static const char *s = \"tmpfs_lookup\";\n"
                         "\tp = &fat16_fs_type;\n"
                         "\tq = tmpfs_private(3);\n"),
        "fs/minix/c.c": ("#if CONFIG_ENABLE_FS_FAT16\n#endif\n"
                         "#if CONFIG_ENABLE_FS_MINIX\n#endif\n"
                         "int minix_local(int x)\n{\n\treturn x;\n}\n"),
        "fs/tmpfs/t.c": ("int tmpfs_lookup(int x)\n{\n\treturn x;\n}\n"
                         "static int tmpfs_private(int x)\n{\n\treturn x;\n}\n"
                         "struct vfs_fstype tmpfs_fs_type = {\n};\n"),
        "fs/fat16/f.c": "struct vfs_fstype fat16_fs_type = {\n};\n",
        "fs/9p/p.c": "#if CONFIG_FS_9P\n#endif\n",
        "fs/vfs_x.c": ("/* CONFIG_ENABLE_FS_TMPFS in a comment */\n"
                       "#if CONFIG_ENABLE_FS_TMPFS\n#endif\n"
                       "#include \"minix/minix_fs.h\"\n"),
        "scripts/make/fs/fat16.mk": ("# CONFIG_ENABLE_FS_TMPFS in a comment\n"
                                     "fs-obj-$(CONFIG_ENABLE_FS_FAT16) += f.o\n"
                                     "fs-obj-$(CONFIG_ENABLE_FS_TMPFS) += g.o\n"),
        "scripts/make/fs.mk": "include $(FS_FRAGMENTS)\n",
    }
    expected = [
        ("fs/minix/a.c", 2), ("fs/minix/a.c", 3),
        ("fs/minix/b.c", 3), ("fs/minix/b.c", 7),
        ("fs/minix/c.c", 1),
        ("fs/vfs_x.c", 2), ("fs/vfs_x.c", 4),
        ("scripts/make/fs/fat16.mk", 3),
    ]
    got = [(rel, line) for rel, line, _ in domain_violations(spec, files)]
    if got != expected:
        return [f"[domain-isolation] self-test: expected {expected}, got {got}"]
    return []


def check_domain_isolation():
    """Members of an isolated domain (fs/<name>/) never reach another
    member, and the domain core selects no member by configuration."""
    errors = _domain_isolation_self_test()
    for spec in DOMAINS:
        for rel, lineno, message in domain_violations(spec,
                                                      _domain_files(spec)):
            errors.append(f"[domain-isolation] {rel}:{lineno}: {message}")
    return errors


# kernel/ holds subdirectories by responsibility plus one loose file,
# kernel/main.c (kmain(), the boot order). Any other source directly under
# kernel/ belongs to a subdirectory, lib/ or its owner subsystem. No ledger.
KERNEL_LOOSE_ALLOWED = frozenset({"main.c"})
KERNEL_LOOSE_SUFFIXES = (".c", ".h", ".S", ".asm")


def kernel_loose_files(kernel_dir: Path) -> list[str]:
    """Return the names of source files directly in @kernel_dir that are not
    the allowed loose file."""
    if not kernel_dir.is_dir():
        return []
    return sorted(p.name for p in kernel_dir.iterdir()
                  if p.is_file() and p.suffix in KERNEL_LOOSE_SUFFIXES
                  and p.name not in KERNEL_LOOSE_ALLOWED)


def _kernel_loose_self_test() -> list[str]:
    """Inject loose files next to main.c; the scanner must report exactly
    those, and nothing in subdirectories or build output."""
    files = ("main.c", "errno.c", "errno.h", "entry.S", "main.o", "main.d",
             "boot/boot_init.c", "sys/utsname.c")
    with tempfile.TemporaryDirectory(prefix="ir0-kloose-") as tmp:
        kdir = Path(tmp) / "kernel"
        for rel in files:
            path = kdir / rel
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n", encoding="utf-8")
        got = kernel_loose_files(kdir)
    expected = ["entry.S", "errno.c", "errno.h"]
    if got != expected:
        return [f"[kernel-loose-files] self-test: expected {expected}, "
                f"got {got}"]
    return []


def check_kernel_loose_files():
    """kernel/main.c is the only source file directly under kernel/."""
    errors = _kernel_loose_self_test()
    for name in kernel_loose_files(ROOT / "kernel"):
        errors.append(
            f"[kernel-loose-files] kernel/{name}: kernel/ keeps one loose "
            "file, kernel/main.c; move it to its kernel/<responsibility>/ "
            "subdirectory, lib/ or its owner subsystem "
            "(rules/ir0-source-layout.md)")
    return errors


# Documentation/ai_driven_dev/rules-digest.md summarizes every rule file: one
# "### <file name>" section per file in rules/, and every section names an
# existing rule file. Keeps the digest from going stale. No ledger.
RULES_DIR = Path("Documentation/ai_driven_dev/rules")
RULES_DIGEST = Path("Documentation/ai_driven_dev/rules-digest.md")
RULES_DIGEST_HEADING_RE = re.compile(r"^### (\S+)[ \t]*$", re.M)


def rules_digest_errors(rules_dir: Path, digest: Path) -> list[str]:
    """Compare the rule files in @rules_dir with the sections of @digest."""
    rule_names = ({p.name for p in rules_dir.iterdir() if p.is_file()}
                  if rules_dir.is_dir() else set())
    if not digest.is_file():
        return [f"[rules-digest] {RULES_DIGEST}: digest is missing"]
    sections = RULES_DIGEST_HEADING_RE.findall(
        digest.read_text(encoding="utf-8"))
    errors = []
    for name in sorted(rule_names - set(sections)):
        errors.append(
            f"[rules-digest] {RULES_DIGEST}: no '### {name}' section for "
            f"{RULES_DIR}/{name}")
    for name in sorted(set(sections) - rule_names):
        errors.append(
            f"[rules-digest] {RULES_DIGEST}: section '### {name}' names no "
            f"file in {RULES_DIR}/")
    for name in sorted({n for n in sections if sections.count(n) > 1}):
        errors.append(
            f"[rules-digest] {RULES_DIGEST}: duplicate section '### {name}'")
    return errors


def _rules_digest_self_test() -> list[str]:
    """Inject a rule without a section, a section without a rule and a
    duplicate section; the check must report exactly those, and accept the
    consistent tree."""
    with tempfile.TemporaryDirectory(prefix="ir0-rdigest-") as tmp:
        rules = Path(tmp) / "rules"
        rules.mkdir()
        digest = Path(tmp) / "rules-digest.md"
        for name in ("a.md", "b.mdc"):
            (rules / name).write_text("x\n", encoding="utf-8")
        digest.write_text("### a.md\n- x\n### b.mdc\n- x\n",
                          encoding="utf-8")
        clean = rules_digest_errors(rules, digest)
        (rules / "new.md").write_text("x\n", encoding="utf-8")
        digest.write_text("### a.md\n### a.md\n### b.mdc\n### gone.md\n",
                          encoding="utf-8")
        bad = rules_digest_errors(rules, digest)
    errors = []
    if clean:
        errors.append(f"[rules-digest] self-test: consistent tree "
                      f"reported {clean}")
    wanted = ("no '### new.md'", "names no file", "duplicate section")
    for needle in wanted:
        if not any(needle in e for e in bad):
            errors.append(f"[rules-digest] self-test: missed '{needle}'")
    return errors


def check_rules_digest():
    """Every rules/ file has a digest section and every section a file."""
    return _rules_digest_self_test() + rules_digest_errors(
        ROOT / RULES_DIR, ROOT / RULES_DIGEST)


def check_portable_no_register_names():
    """Reject new ISA register names in portable identifiers and stale
    ledger lines."""
    return _register_name_self_test() + register_name_errors(
        register_name_violations(), _load_register_name_baseline())


def print_register_name_baseline():
    print("# Register names in portable identifiers: frozen offenders.")
    print("# Format: <path> <identifier>")
    print("# The ledger only shrinks. arch-guard fails on a pair that is not "
          "listed")
    print("# and on a listed pair whose identifier is gone: a branch that "
          "fixes an")
    print("# offender deletes its line here in the same commit.")
    print("# Regenerate: python3 scripts/architecture_guard.py "
          "--print-register-baseline")
    for rel, ident in sorted(register_name_violations()):
        print(f"{rel} {ident}")


# mm/pmm.h and mm/paging.h moved into their <ir0/...> facades and the old
# forwarders are gone. The ledger stays empty; a file listed here may keep the
# old spelling until it migrates (the list only shrinks).
RETIRED_MM_HEADER_RE = re.compile(
    r'^\s*#\s*include\s*(?:<mm/(pmm|paging)\.h>|"(pmm|paging)\.h")')
RETIRED_MM_HEADER_USERS: frozenset[str] = frozenset()


def check_retired_mm_headers():
    """PMM and paging are declared once, in <ir0/pmm.h> and <ir0/paging.h>."""
    errors = []
    users: set[str] = set()
    for tree in SINGLE_DECL_TREES:
        for fpath in iter_c_files(ROOT / tree):
            rel = str(rel_to_root(fpath))
            for lineno, line in enumerate(read_file_text(fpath).splitlines(), 1):
                match = RETIRED_MM_HEADER_RE.match(line)
                if not match:
                    continue
                users.add(rel)
                if rel not in RETIRED_MM_HEADER_USERS:
                    name = match.group(1) or match.group(2)
                    errors.append(
                        f"[retired-mm-header] {rel}:{lineno}: include "
                        f"<ir0/{name}.h> instead"
                    )
    for rel in sorted(RETIRED_MM_HEADER_USERS - users):
        errors.append(
            f"[retired-mm-header] {rel} no longer uses the old path; drop it "
            "from RETIRED_MM_HEADER_USERS"
        )
    return errors


# Retired public API names (one API per behavior). Every converged family
# lists its deleted variants in RETIRED_API_LEDGER as "<name> <use instead>";
# the name may not come back anywhere in kernel C sources (comments and
# string literals excluded). The ledger is strict: a malformed or duplicate
# line fails the guard instead of being ignored.
RETIRED_API_LEDGER = ROOT / "scripts" / "retired_api_names.txt"
RETIRED_API_TREES = ("arch", "block", "drivers", "fs", "includes", "ipc",
                     "kernel", "ktm", "lib", "mm", "net", "sched", "smp",
                     "sound")
_RETIRED_API_LINE_RE = re.compile(r"^([A-Za-z_]\w*)\s+(\S.*)$")


def load_retired_api_ledger(text: str) -> tuple[dict[str, str], list[str]]:
    """Parse the ledger; return ({name: replacement}, errors)."""
    names: dict[str, str] = {}
    errors = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = _RETIRED_API_LINE_RE.match(line)
        if not match:
            errors.append(f"[retired-api] ledger line {lineno}: expected "
                          f"'<name> <use instead>', got '{line}'")
            continue
        name, hint = match.groups()
        if name in names:
            errors.append(f"[retired-api] ledger line {lineno}: duplicate "
                          f"entry '{name}'")
            continue
        names[name] = hint
    return names, errors


def retired_api_violations(text: str, names: dict[str, str]) -> list[tuple[int, str]]:
    """Return (line, name) for each retired name used in C *text*."""
    if not names:
        return []
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    text = _C_STRING_RE.sub('""', text)
    pattern = re.compile(r"\b(" + "|".join(sorted(map(re.escape, names))) + r")\b")
    found = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for match in pattern.finditer(line):
            found.append((lineno, match.group(1)))
    return found


def _retired_api_self_test() -> list[str]:
    """Inject retired names and ledger defects; the scanner must catch them."""
    errors = []
    names, ledger_errors = load_retired_api_ledger(
        "# comment\nold_api new_api\nold_api again\nbroken\n")
    if names != {"old_api": "new_api"} or len(ledger_errors) != 2:
        errors.append("[retired-api] self-test: ledger parser accepted a "
                      f"duplicate or malformed line: {ledger_errors}")
    sample = ("/* old_api() in a comment is fine */\n"
              "static const char *m = \"old_api\";\n"
              "int old_api_ok(void);\n"
              "\told_api(1);\n")
    got = retired_api_violations(sample, names)
    if got != [(4, "old_api")]:
        errors.append(f"[retired-api] self-test: expected [(4, 'old_api')], "
                      f"got {got}")
    return errors


def check_retired_api_names():
    """Reject every name listed in the retired-API ledger."""
    errors = _retired_api_self_test()
    names, ledger_errors = load_retired_api_ledger(
        read_file_text(RETIRED_API_LEDGER))
    errors.extend(ledger_errors)
    for tree in RETIRED_API_TREES:
        for path in iter_c_files(ROOT / tree):
            rel = rel_to_root(path)
            for lineno, name in retired_api_violations(read_file_text(path),
                                                       names):
                errors.append(
                    f"[retired-api] {rel}:{lineno}: {name} was retired; "
                    f"use {names[name]}"
                )
    return errors


# Every function a project header declares has a definition somewhere in the
# tree (C, C++, Rust, or an assembly label, macro-generated labels included).
# A prototype with no definition is dead surface that hides a link error until
# someone calls it. Entries in UNDEFINED_PROTOTYPE_LEDGER ("<name> <header>
# <reason>") may stay while the work that owns their header lands; an entry
# whose prototype is gone or now defined fails the guard, so the list only
# shrinks. Userspace wrapper headers are not kernel API (SYS-6 moves them).
UNDEFINED_PROTOTYPE_LEDGER = ROOT / "scripts" / "undefined_prototypes.txt"
UNDEFINED_PROTOTYPE_SKIP = (
    "includes/uapi/",
    "includes/ir0/syscall.h",
)
_UNDEFINED_PROTOTYPE_SKIP_RE = re.compile(r"^arch/[^/]+/include/uapi/")
DEFINITION_TREES = SINGLE_DECL_TREES + ("cpp", "rust")
_C_DEFINITION_RE = re.compile(
    r"\b([A-Za-z_]\w*)\s*\([^()]*(?:\([^()]*(?:\([^()]*\)[^()]*)*\)[^()]*)*\)\s*"
    r"(?:__attribute__\s*\(\([^;{]*?\)\)\s*)*\{"
)
_RUST_DEFINITION_RE = re.compile(r"\bfn\s+([A-Za-z_]\w*)\s*[(<]")
_ASM_LABEL_RE = re.compile(r"^[ \t]*([A-Za-z_.%\\][\w.%\\{}()]*)[ \t]*:", re.M)
_ASM_MACRO_ARG_RE = re.compile(r"%\{?\w+\}?|\\\(\)|\\\w+")
_UNDEFINED_LEDGER_LINE_RE = re.compile(r"^([A-Za-z_]\w*)\s+(\S+)\s+(\S.*)$")


def defined_function_names(rel: str, text: str) -> tuple[set[str], list[re.Pattern]]:
    """Return (names, label templates) a source file defines.

    C/C++ definitions are "name(...) {" after comments are blanked. Assembly
    labels written inside macros ("isr%1_64:", "entry_\\n:") become templates
    in which every macro argument matches any identifier fragment.
    """
    if rel.endswith(".rs"):
        return set(_RUST_DEFINITION_RE.findall(text)), []
    if rel.endswith((".S", ".s", ".asm")):
        names: set[str] = set()
        templates = []
        for label in _ASM_LABEL_RE.findall(text):
            if _ASM_MACRO_ARG_RE.search(label):
                parts = _ASM_MACRO_ARG_RE.split(label)
                templates.append(re.compile(r"\w*".join(map(re.escape, parts))))
            else:
                names.add(label)
        return names, templates
    text = _DECL_COMMENT_RE.sub(" ", text)
    return set(_C_DEFINITION_RE.findall(text)), []


def load_undefined_prototype_ledger(text: str) -> tuple[dict[str, str], list[str]]:
    """Parse the ledger; return ({name: header}, errors)."""
    names: dict[str, str] = {}
    errors = []
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = _UNDEFINED_LEDGER_LINE_RE.match(line)
        if not match:
            errors.append(f"[undefined-prototype] ledger line {lineno}: "
                          f"expected '<name> <header> <reason>', got '{line}'")
            continue
        name, header, _reason = match.groups()
        if name in names:
            errors.append(f"[undefined-prototype] ledger line {lineno}: "
                          f"duplicate entry '{name}'")
            continue
        names[name] = header
    return names, errors


def undefined_prototypes(headers: dict[str, str],
                         sources: dict[str, str]) -> list[tuple[str, str]]:
    """Return sorted (header, name) for prototypes no source defines."""
    defined: set[str] = set()
    templates: list[re.Pattern] = []
    for rel, text in sources.items():
        names, tmpl = defined_function_names(rel, text)
        defined |= names
        templates.extend(tmpl)
    found = []
    for rel, text in headers.items():
        for name in _header_prototypes(text):
            if name in defined or any(t.fullmatch(name) for t in templates):
                continue
            found.append((rel, name))
    return sorted(found)


def _undefined_prototype_self_test() -> list[str]:
    """Inject an undefined prototype and ledger defects; the check must fire."""
    errors = []
    names, ledger_errors = load_undefined_prototype_ledger(
        "# c\nold_fn a.h owner\nold_fn a.h again\nbroken\n")
    if names != {"old_fn": "a.h"} or len(ledger_errors) != 2:
        errors.append("[undefined-prototype] self-test: ledger parser "
                      f"accepted a duplicate or malformed line: {ledger_errors}")
    headers = {"a.h": "int used(void);\nint ghost(int x);\n"
                      "void isr7_64(void);\nint rs_fn(void);\n"
                      "static inline int inl(void) { return 0; }\n"}
    sources = {
        "a.c": "/* int ghost(int x) { } */\nint used(void)\n{\n\treturn 0;\n}\n",
        "b.asm": "%macro ISR 1\nglobal isr%1_64\nisr%1_64:\n\tiretq\n%endmacro\n",
        "c.rs": "pub extern \"C\" fn rs_fn() -> i32 { 0 }\n",
    }
    got = undefined_prototypes(headers, sources)
    if got != [("a.h", "ghost")]:
        errors.append("[undefined-prototype] self-test: expected "
                      f"[('a.h', 'ghost')], got {got}")
    return errors


def check_undefined_prototypes():
    """Reject a project-header prototype that nothing in the tree defines."""
    errors = _undefined_prototype_self_test()
    ledger, ledger_errors = load_undefined_prototype_ledger(
        read_file_text(UNDEFINED_PROTOTYPE_LEDGER))
    errors.extend(ledger_errors)
    headers: dict[str, str] = {}
    sources: dict[str, str] = {}
    for tree in DEFINITION_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIR_NAMES)
            for name in sorted(filenames):
                path = Path(dirpath) / name
                rel = str(rel_to_root(path))
                if name.endswith(".h"):
                    if (tree in SINGLE_DECL_TREES
                            and not rel.startswith(UNDEFINED_PROTOTYPE_SKIP)
                            and not _UNDEFINED_PROTOTYPE_SKIP_RE.match(rel)):
                        headers[rel] = read_file_text(path)
                elif name.endswith((".c", ".cpp", ".rs", ".S", ".s", ".asm")):
                    sources[rel] = read_file_text(path)
    seen: set[str] = set()
    for rel, name in undefined_prototypes(headers, sources):
        if ledger.get(name) == rel:
            seen.add(name)
            continue
        errors.append(f"[undefined-prototype] {rel}: {name} is declared but "
                      "never defined; delete the prototype")
    for name in sorted(set(ledger) - seen):
        errors.append(f"[undefined-prototype] stale ledger entry '{name}': "
                      "the prototype is gone or defined, remove the entry")
    return errors


INCLUDE_LAYOUT_TREES = SINGLE_DECL_TREES
_LAYOUT_INCLUDE_RE = re.compile(r'^\s*#\s*include\s*([<"][^>"]+[>"])')
_LAYOUT_ALLMAN_HEAD_RE = re.compile(r"\)\s*(?:__attribute__\s*\(\(.*\)\)\s*)*$")
_LAYOUT_KR_HEAD_RE = re.compile(r"^[A-Za-z_][^;=#]*\)\s*\{\s*$")

# Existing offenders, keyed "<kind> <path> <header>". Fix the file and drop
# its entry; an entry whose violation is gone fails the guard so the list
# only shrinks.
INCLUDE_LAYOUT_BASELINE = frozenset({
})


def include_layout_violations(text: str) -> list[tuple[str, str, int]]:
    """Return (kind, header, line) for duplicate and late #include lines.

    Comments are blanked first, keeping line numbers. A function body starts
    at an Allman "{" line under a ")" head, or at a K&R "...) {" line; an
    include after that point is late.
    """
    text = _DECL_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    seen: set[str] = set()
    found: list[tuple[str, str, int]] = []
    in_body = False
    prev = ""
    for lineno, line in enumerate(text.split("\n"), start=1):
        match = _LAYOUT_INCLUDE_RE.match(line)
        if match:
            header = match.group(1)
            if in_body:
                found.append(("late", header, lineno))
            if header in seen:
                found.append(("duplicate", header, lineno))
            seen.add(header)
            continue
        if not in_body:
            allman = (line.rstrip() == "{"
                      and not prev.lstrip().startswith("#")
                      and _LAYOUT_ALLMAN_HEAD_RE.search(prev))
            if allman or _LAYOUT_KR_HEAD_RE.match(line):
                in_body = True
        if line.strip():
            prev = line.rstrip()
    return found


def check_include_layout(paths=None, baseline=INCLUDE_LAYOUT_BASELINE):
    """Each header is included once, and before the first function body."""
    errors = []
    hit: set[str] = set()
    whole_tree = paths is None
    if whole_tree:
        paths = [p for tree in INCLUDE_LAYOUT_TREES
                 for p in iter_c_files(ROOT / tree)]
    for fpath in paths:
        try:
            text = read_file_text(fpath)
        except OSError as exc:
            errors.append(f"[read-error] {fpath}: {exc}")
            continue
        rel = str(rel_to_root(fpath))
        for kind, header, lineno in include_layout_violations(text):
            key = f"{kind} {rel} {header}"
            if key in baseline:
                hit.add(key)
                continue
            where = ("after the first function definition; move it to the "
                     "include block" if kind == "late"
                     else "already included above; drop this line")
            errors.append(f"[include-layout] {rel}:{lineno}: {header} {where}")
    if whole_tree:
        for key in sorted(baseline - hit):
            errors.append(
                f"[include-layout] stale baseline entry '{key}': the file "
                "is fixed, remove the entry"
            )
    return errors


def self_test_include_layout() -> list[str]:
    """Inject each layout violation into a scratch file; the check must fire."""
    cases = {
        "clean.c": ("#include <a.h>\n#include \"b.h\"\n\n"
                    "/* #include <a.h> in a comment */\n"
                    "static const int t[] =\n{\n\t1,\n};\n\n"
                    "int f(void)\n{\n\treturn t[0];\n}\n", []),
        "dup.c": ("#include <a.h>\n#include <b.h>\n#include <a.h>\n\n"
                  "int f(void)\n{\n\treturn 0;\n}\n",
                  [("duplicate", "<a.h>", 3)]),
        "late_allman.c": ("#include <a.h>\n\nint f(void)\n{\n\treturn 0;\n}\n"
                          "\n#include <b.h>\n",
                          [("late", "<b.h>", 8)]),
        "late_kr.h": ("#pragma once\nstatic inline int f(int x) {\n"
                      "\treturn x;\n}\n#include \"c.h\"\n",
                      [("late", "\"c.h\"", 5)]),
    }
    failures = []
    with tempfile.TemporaryDirectory(prefix="ir0-layout-") as tmp:
        for name, (text, expected) in cases.items():
            path = Path(tmp) / name
            path.write_text(text, encoding="utf-8")
            got = include_layout_violations(path.read_text(encoding="utf-8"))
            if got != expected:
                failures.append(f"{name}: expected {expected}, got {got}")
    return failures


# UPROG-1: the kernel never knows which userspace program runs. Code paths,
# workarounds and comments state the Linux rule they implement (POSIX, the
# man pages, Linux behaviour), not the program that needed it. The names
# live in scripts/smp_entry_gate/userspace_programs.txt (one per line) and
# the reviewed false positives in userspace_programs_ledger.txt
# ("<path> <name>  # why"; an entry with no hit left is stale). The SMP
# entry gate (criterion 2) runs this same scan through
# userspace_program_findings(). A name matches as a whole word, without
# regard to case, in code, comments and strings: "ash" leaves "hash" and
# "flash" alone, "ping" leaves "mapping" alone.
USERSPACE_DATA = ROOT / "scripts" / "smp_entry_gate"
USERSPACE_NAMES_FILE = USERSPACE_DATA / "userspace_programs.txt"
USERSPACE_LEDGER_FILE = USERSPACE_DATA / "userspace_programs_ledger.txt"
USERSPACE_TREES = ("arch", "block", "cpp", "drivers", "fs", "includes", "ipc",
                   "kernel", "ktm", "lib", "mm", "net", "rust", "sched", "smp",
                   "sound")
USERSPACE_SKIP_PREFIXES = ("kernel/test/", "ktm/mocks/", "includes/uapi/")
USERSPACE_SUFFIXES = (".c", ".h", ".S", ".asm", ".cpp", ".hpp", ".rs")
_USERSPACE_NAME_RE = re.compile(r"^[a-z0-9][a-z0-9+._-]*$")


def _data_lines(text: str):
    for lineno, raw in enumerate(text.splitlines(), 1):
        line = raw.split("#", 1)[0].strip()
        if line:
            yield lineno, line


def load_userspace_names(text: str) -> tuple[set[str], list[str]]:
    """Parse the name list; return (names, errors)."""
    names: set[str] = set()
    errors = []
    for lineno, line in _data_lines(text):
        name = line.lower()
        if not _USERSPACE_NAME_RE.match(name):
            errors.append(f"userspace_programs.txt:{lineno}: expected one "
                          f"program name, got '{line}'")
        elif name in names:
            errors.append(f"userspace_programs.txt:{lineno}: duplicate "
                          f"name '{name}'")
        else:
            names.add(name)
    return names, errors


def load_userspace_ledger(text: str, names: set[str]):
    """Parse the ledger; return ({(path, name): lineno}, errors)."""
    entries: dict[tuple[str, str], int] = {}
    errors = []
    for lineno, line in _data_lines(text):
        fields = line.split()
        if len(fields) != 2 or fields[1].lower() not in names:
            errors.append(f"userspace_programs_ledger.txt:{lineno}: expected "
                          "'<path> <listed name>  # why'")
            continue
        key = (fields[0], fields[1].lower())
        if key in entries:
            errors.append(f"userspace_programs_ledger.txt:{lineno}: "
                          "duplicate entry")
        entries[key] = lineno
    return entries, errors


def userspace_name_pattern(names: set[str]) -> re.Pattern:
    return re.compile(r"\b(" + "|".join(sorted(map(re.escape, names))) +
                      r")\b", re.IGNORECASE)


def userspace_name_hits(text: str, pattern: re.Pattern):
    """Return (line, name, source line) for every listed name in *text*."""
    found = []
    for lineno, line in enumerate(text.splitlines(), 1):
        for match in pattern.finditer(line):
            found.append((lineno, match.group(1).lower(), line))
    return found


def _userspace_files() -> list[str]:
    files = []
    for tree in USERSPACE_TREES:
        base = ROOT / tree
        if not base.is_dir():
            continue
        for dirpath, dirnames, filenames in os.walk(base):
            dirnames[:] = sorted(d for d in dirnames if d not in SKIP_DIR_NAMES)
            for fname in sorted(filenames):
                if fname.endswith(USERSPACE_SUFFIXES):
                    rel = str(rel_to_root(Path(dirpath) / fname))
                    if not rel.startswith(USERSPACE_SKIP_PREFIXES):
                        files.append(rel)
    return sorted(files)


def userspace_program_findings() -> tuple[list[str], list[str]]:
    """(hits "path:line: name: text", data and stale-ledger errors)."""
    names, errors = load_userspace_names(read_file_text(USERSPACE_NAMES_FILE))
    if not names:
        return [], errors + ["userspace_programs.txt lists no names"]
    ledger, ledger_errors = load_userspace_ledger(
        read_file_text(USERSPACE_LEDGER_FILE), names)
    errors.extend(ledger_errors)
    pattern = userspace_name_pattern(names)
    hits = []
    covered = set()
    for rel in _userspace_files():
        for lineno, name, line in userspace_name_hits(
                read_file_text(ROOT / rel), pattern):
            if (rel, name) in ledger:
                covered.add((rel, name))
                continue
            hits.append(f"{rel}:{lineno}: {name}: {line.strip()[:100]}")
    for key in sorted(set(ledger) - covered):
        errors.append(f"userspace_programs_ledger.txt:{ledger[key]}: stale "
                      f"entry '{key[0]} {key[1]}': no hit left, drop it")
    return hits, errors


def _userspace_agnostic_self_test() -> list[str]:
    """Inject program names and near misses; the scanner must tell them apart."""
    errors = []
    names, name_errors = load_userspace_names(
        "# comment\nash\nping\nvi\nash\nBad Name\n")
    if names != {"ash", "ping", "vi"} or len(name_errors) != 2:
        errors.append("[userspace-agnostic] self-test: name parser accepted "
                      f"a defect: {sorted(names)} {name_errors}")
    ledger, ledger_errors = load_userspace_ledger(
        "a.c ping  # why\na.c ping\nb.c nope\nbroken\n", names)
    if set(ledger) != {("a.c", "ping")} or len(ledger_errors) != 3:
        errors.append("[userspace-agnostic] self-test: ledger parser accepted "
                      f"a defect: {ledger_errors}")
    sample = ("/* hash flash crash mapping typing ashes vivid via */\n"
              "static int busy; /* ASH spins */\n"
              "\t/* ping 10.0.0.1 */\n"
              "/* open it in vi */\n")
    got = [(line, name) for line, name, _text in
           userspace_name_hits(sample, userspace_name_pattern(names))]
    want = [(2, "ash"), (3, "ping"), (4, "vi")]
    if got != want:
        errors.append(f"[userspace-agnostic] self-test: expected {want}, "
                      f"got {got}")
    return errors


def check_userspace_agnostic():
    """Reject userspace program names anywhere in the kernel sources."""
    errors = _userspace_agnostic_self_test()
    hits, data_errors = userspace_program_findings()
    errors.extend(f"[userspace-agnostic] {e}" for e in data_errors)
    errors.extend(f"[userspace-agnostic] {h}  (state the Linux rule instead)"
                  for h in hits)
    return errors



# A policy is any sched/*.c other than the core, the idle loop and the
# trace hook that defines sched_pick_next. Every policy implements the same
# private contract; schedule() and the context switch stay in sched/core.c.
# sched_pick_next() is declared in sched/core.h, not in the public facade.
# A future sched/cfs.c is covered once it defines sched_pick_next. No ledger.
SCHED_POLICY_SKIP = frozenset({"core.c", "idle.c", "trace.c"})
SCHED_POLICY_REQUIRED = (
    "sched_add",
    "sched_remove",
    "sched_promote",
    "sched_count_runnable",
    "sched_pick_next",
    "sched_policy_name",
    "sched_selftest",
)
SCHED_POLICY_CORE_ONLY = ("schedule", "sched_switch_to")
SCHED_POLICY_INCLUDES = frozenset({"rr.c", "prio.c", "cfs.c", "idle.c"})
SCHED_PUBLIC_DECLS = (
    "schedule",
    "sched_switch_to",
    "sched_take_resched",
    "sched_add",
    "sched_remove",
    "sched_promote",
    "sched_count_runnable",
    "sched_policy_name",
    "sched_selftest",
)


def _sched_source(text: str) -> str:
    """Blank comments and strings, keeping line numbers."""
    text = _C_COMMENT_RE.sub(lambda m: "\n" * m.group(0).count("\n"), text)
    text = _C_LINE_COMMENT_RE.sub("", text)
    return _C_STRING_RE.sub('""', text)


def _sched_definitions(text: str) -> dict[str, int]:
    """First definition line of each function in one translation unit."""
    code = _sched_source(text)
    found: dict[str, int] = {}
    for match in _C_DEFINITION_RE.finditer(code):
        name = match.group(1)
        if name not in found:
            found[name] = code.count("\n", 0, match.start()) + 1
    return found


def sched_policy_errors(sched_dir: Path) -> list[str]:
    """Policy files directly under @sched_dir that break the contract."""
    if not sched_dir.is_dir():
        return []
    errors = []
    for path in sorted(sched_dir.glob("*.c")):
        if path.name in SCHED_POLICY_SKIP:
            continue
        source = path.read_text(encoding="utf-8", errors="replace")
        defined = _sched_definitions(source)
        if "sched_pick_next" not in defined:
            continue
        rel = f"sched/{path.name}"
        for name in SCHED_POLICY_REQUIRED:
            if name not in defined:
                errors.append(
                    f"[sched-policy] {rel}: does not define {name}")
        for name in SCHED_POLICY_CORE_ONLY:
            if name in defined:
                errors.append(
                    f"[sched-policy] {rel}:{defined[name]}: defines {name}; "
                    "that stays in sched/core.c")
        code = _sched_source(source)
        for match in _DOMAIN_INCLUDE_RE.finditer(code):
            header = match.group(1)
            base = header.replace("\\", "/").rsplit("/", 1)[-1]
            if base not in SCHED_POLICY_INCLUDES:
                continue
            lineno = code.count("\n", 0, match.start()) + 1
            errors.append(
                f"[sched-policy] {rel}:{lineno}: includes {header}; "
                "a policy does not include another policy")
    return errors


def sched_facade_errors(text: str, rel: str) -> list[str]:
    """The public facade keeps its declarations and not sched_pick_next."""
    declared = _header_prototypes(text)
    errors = []
    for name in SCHED_PUBLIC_DECLS:
        if name not in declared:
            errors.append(
                f"[sched-policy] {rel}: public facade does not declare "
                f"{name}")
    if "sched_pick_next" in declared:
        errors.append(
            f"[sched-policy] {rel}: declares sched_pick_next; "
            "that prototype stays in sched/core.h")
    return errors


def _sched_policy_stub(names) -> str:
    return "".join(f"void {name}(void)\n{{\n}}\n" for name in names)


def _sched_policy_self_test() -> list[str]:
    """Inject a policy that omits sched_promote and one that defines
    schedule. Both must be reported, then the scratch files are removed."""
    omit = _sched_policy_stub(
        name for name in SCHED_POLICY_REQUIRED if name != "sched_promote")
    takeover = _sched_policy_stub((*SCHED_POLICY_REQUIRED, "schedule"))
    failures = []
    got: list[str] = []
    facade_got: list[str] = []
    scratch: list[Path] = []
    with tempfile.TemporaryDirectory(prefix="ir0-sched-policy-") as tmp:
        sched = Path(tmp) / "sched"
        sched.mkdir()
        scratch = [sched / "omit_promote.c", sched / "defines_schedule.c"]
        scratch[0].write_text(omit, encoding="utf-8")
        scratch[1].write_text(takeover, encoding="utf-8")
        got = sched_policy_errors(sched)
        for path in scratch:
            path.unlink()
        facade_rel = "includes/ir0/sched.h"
        facade_got = sched_facade_errors(
            (ROOT / facade_rel).read_text(encoding="utf-8"), facade_rel)
        live = sched_policy_errors(ROOT / "sched")
    leftovers = [p.name for p in scratch if p.exists()]
    if leftovers:
        failures.append(
            "[sched-policy] self-test: scratch files left behind: "
            + ", ".join(leftovers))
    if not any("omit_promote.c" in err and "sched_promote" in err
               for err in got):
        failures.append(
            "[sched-policy] self-test: missed a policy that omits "
            f"sched_promote ({got})")
    if not any("defines_schedule.c" in err and "defines schedule" in err
               for err in got):
        failures.append(
            "[sched-policy] self-test: missed a policy that defines "
            f"schedule ({got})")
    if live:
        failures.append(
            "[sched-policy] self-test: current policies reported "
            f"{live}")
    if facade_got:
        failures.append(
            "[sched-policy] self-test: public facade reported "
            f"{facade_got}")
    return failures


def check_sched_policy_contract():
    """Each scheduler policy implements the same private contract."""
    errors = _sched_policy_self_test()
    errors.extend(sched_policy_errors(ROOT / "sched"))
    facade = ROOT / "includes" / "ir0" / "sched.h"
    errors.extend(sched_facade_errors(
        read_file_text(facade), "includes/ir0/sched.h"))
    return errors


# Debt ledgers: reviewed exceptions that only shrink. The SMP entry gate
# (scripts/smp_entry_gate.py) needs every one of them empty. Files count
# their non-comment lines; in-module ledgers count their entries.
# RETIRED_API_LEDGER is not debt: it is the deny-list of retired names and
# never empties.
DEBT_LEDGER_FILES = (REGISTER_NAME_BASELINE, UNDEFINED_PROTOTYPE_LEDGER)
DEBT_LEDGER_SETS = {
    "RAW_ATOMIC_LEDGER": RAW_ATOMIC_LEDGER,
    "INCLUDE_LAYOUT_BASELINE": INCLUDE_LAYOUT_BASELINE,
}


def main():
    if "--self-test" in sys.argv[1:]:
        failures = (self_test_include_layout() + _busy_wait_self_test() +
                    _register_name_self_test() + _uaccess_self_test() +
                    _raw_atomic_self_test() +
                    _undefined_prototype_self_test() +
                    _legacy_pc_self_test() +
                    _domain_isolation_self_test() +
                    _kernel_loose_self_test() +
                    _rules_digest_self_test() +
                    _userspace_agnostic_self_test() +
                    _sched_policy_self_test())
        if failures:
            print("[arch-guard] self-test FAILED")
            for failure in failures:
                print(" -", failure)
            return 1
        print("[arch-guard] self-test OK (include-layout, no-busy-wait, "
              "portable-no-register-names, uaccess-single-api, "
              "no-raw-atomics, undefined-prototype, legacy-pc-io, "
              "domain-isolation, kernel-loose-files, rules-digest, "
              "userspace-agnostic, sched-policy)")
        return 0

    errors = []
    errors.extend(check_forbidden_includes())
    errors.extend(check_facades())
    errors.extend(check_semantic_tls_contract())
    errors.extend(check_process_fd_lifecycle_boundary())
    errors.extend(check_facade_no_drivers_include())
    errors.extend(check_facade_no_kernel_include())
    errors.extend(check_arm64_scaffold())
    errors.extend(check_interrupt_arch_portable())
    errors.extend(check_fs_no_direct_arch())
    errors.extend(check_mm_net_no_arch_includes())
    errors.extend(check_portable_trees_no_kernel_headers())
    errors.extend(check_drivers_ir0_block_dev_only())
    errors.extend(check_drivers_no_arch_includes())
    errors.extend(check_single_header_declarations())
    errors.extend(check_kernel_no_driver_includes())
    errors.extend(check_kernel_no_direct_arch_portable())
    errors.extend(check_fs_no_mm_includes())
    errors.extend(check_ktm_no_mm_includes())
    errors.extend(check_bluetooth_subdir_include_policy())
    errors.extend(check_devfs_usercopy_contract())
    errors.extend(check_devfs_unique_device_ids())
    errors.extend(check_devfs_no_find_by_id_outside_devfs())
    errors.extend(check_ktm_mock_boundaries())
    errors.extend(check_hardware_backends_do_not_self_test_log())
    # Keep assembly limited to reviewed, narrow C mechanism callbacks.
    errors.extend(check_asm_c_boundary())
    errors.extend(check_usercopy_no_raw_user_touch())
    errors.extend(check_ktm_core_no_fase())
    errors.extend(check_ktm_no_fase_serial())
    errors.extend(check_klog_serial_print_allowlist())
    errors.extend(check_ktm_angle_includes())
    errors.extend(check_kernel_no_relative_includes())
    errors.extend(check_portable_no_isa_asm())
    errors.extend(check_portable_no_isa_leak_literals())
    errors.extend(check_portable_no_isa_firmware_headers())
    errors.extend(check_portable_no_task_arch_fields())
    errors.extend(check_process_pgd_accessor())
    errors.extend(check_signal_resume_retired())
    errors.extend(check_process_wait_state_accessor())
    errors.extend(check_syscall_frame_accessor())
    errors.extend(check_asm_offsets_sync())
    errors.extend(check_subsystems_json_paths())
    errors.extend(check_kernel_lib_i1_migration())
    errors.extend(check_includes_ir0_no_c_sources())
    errors.extend(check_portable_port1_no_legacy_arch_mm())
    errors.extend(check_portable_no_arch_prefix_calls())
    errors.extend(check_context_backend_include_boundary())
    errors.extend(check_scheduler_user_return_boundary())
    errors.extend(check_scheduler_runqueue_lock_boundary())
    errors.extend(check_process_lifecycle_no_isa_conditionals())
    errors.extend(check_process_registry_ownership())
    errors.extend(check_common_c_has_no_isa_conditionals())
    errors.extend(check_common_headers_have_only_data_model_isa_conditionals())
    errors.extend(check_arch_sources_need_no_compiler_isa_selection())
    errors.extend(check_process_lifecycle_uses_semantic_return_state())
    errors.extend(check_portable_task_api_has_no_x86_register_aliases())
    errors.extend(check_portable_code_has_no_private_asm_headers())
    errors.extend(check_common_paging_uses_neutral_roots())
    errors.extend(check_common_address_space_contracts())
    errors.extend(check_common_elf_uses_isa_policy())
    errors.extend(check_common_mm_uses_user_va_policy())
    errors.extend(check_common_syscall_policy_uses_semantic_ids())
    errors.extend(check_sysv_shm_is_network_independent())
    errors.extend(check_common_mm_uses_tlb_sync())
    errors.extend(check_errno_has_single_catalog())
    errors.extend(check_kmem_single_api())
    errors.extend(check_uaccess_single_api())
    errors.extend(check_portable_no_register_names())
    errors.extend(check_no_busy_wait_primitives())
    errors.extend(check_no_raw_atomics())
    errors.extend(check_no_isa_named_headers())
    errors.extend(check_no_syscall_stubs())
    errors.extend(check_legacy_pc_io())
    errors.extend(check_domain_isolation())
    errors.extend(check_userspace_agnostic())
    errors.extend(check_kernel_loose_files())
    errors.extend(check_rules_digest())
    errors.extend(check_sched_policy_contract())
    errors.extend(check_retired_mm_headers())
    errors.extend(check_retired_api_names())
    errors.extend(check_undefined_prototypes())
    errors.extend(check_include_layout())

    if errors:
        print("[arch-guard] FAILED")
        for err in errors:
            print(" -", err)
        return 1

    print("[arch-guard] OK")
    print("DEVFS_IO_CONTRACT_OK")
    return 0


if __name__ == "__main__":
    if "--print-register-baseline" in sys.argv[1:]:
        print_register_name_baseline()
        sys.exit(0)
    sys.exit(main())
