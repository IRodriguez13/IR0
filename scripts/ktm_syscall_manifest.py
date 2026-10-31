#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
KTM — compare Linux x86-64 syscall numbers vs IR0 dispatch table wiring.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SYSCALL_H = ROOT / "includes/uapi/ir0/syscall_linux.h"
SYSCALL_DISPATCH_C = ROOT / "kernel/syscalls/syscall_dispatch.c"
SYSCALLS_C = ROOT / "kernel/syscalls.c"  # legacy fallback if dispatch not split yet
X86_DECODE_C = ROOT / "arch/x86-64/sources/syscall_decode.c"

TIER1_BUSYBOX = {
    0: "read", 1: "write", 2: "open", 3: "close", 5: "fstat",
    8: "lseek", 9: "mmap", 11: "munmap", 12: "brk", 16: "ioctl",
    22: "pipe", 23: "select", 32: "dup", 33: "dup2", 34: "pause",
    35: "nanosleep", 39: "getpid", 41: "socket", 42: "connect",
    43: "accept", 44: "sendto", 45: "recvfrom", 57: "fork", 59: "execve",
    60: "exit", 61: "wait4", 72: "fcntl", 79: "getcwd", 80: "chdir",
    87: "unlink", 89: "readlink", 90: "chmod", 102: "getuid",
    231: "exit_group", 257: "openat", 262: "newfstatat",
}

MUSL_CRED_SET = {
    13: "rt_sigaction", 14: "rt_sigprocmask", 15: "rt_sigreturn",
    56: "clone", 62: "kill", 115: "getgroups", 116: "setgroups",
    117: "setresuid", 118: "getresuid", 119: "setresgid", 120: "getresgid",
    158: "arch_prctl", 186: "gettid", 202: "futex", 218: "set_tid_address",
    228: "clock_gettime", 231: "exit_group", 234: "tgkill", 273: "set_robust_list",
}

MUSL_SIGNAL_SET = {
    13: "rt_sigaction", 14: "rt_sigprocmask", 15: "rt_sigreturn",
    34: "pause", 62: "kill", 234: "tgkill",
}


def parse_nr_define(path: Path) -> dict[int, str]:
    out: dict[int, str] = {}
    for line in path.read_text().splitlines():
        m = re.match(r"#define\s+__NR_(\w+)\s+(\d+)", line)
        if m:
            out[int(m.group(2))] = m.group(1)
    return out


def parse_socket_loop_wiring(text: str, name_to_nr: dict[str, int]) -> set[int]:
    """Detect socket_nrs[] loop assigning sys_nosys in init_syscall_table()."""
    wired: set[int] = set()
    if "socket_nrs" not in text or "sys_nosys" not in text:
        return wired
    m = re.search(
        r"static const unsigned socket_nrs\[\] = \{(.*?)\};",
        text,
        re.DOTALL,
    )
    if not m:
        return wired
    if not re.search(r"for \(si = 0; si < sizeof\(socket_nrs\)", text):
        return wired
    for token in m.group(1).split(","):
        token = token.strip()
        mm = re.match(r"__NR_(\w+)", token)
        if not mm:
            continue
        nr = name_to_nr.get(mm.group(1))
        if nr is not None:
            wired.add(nr)
    return wired


def parse_wired_table(path: Path) -> set[int]:
    wired: set[int] = set()
    names = parse_nr_define(SYSCALL_H)
    name_to_nr = {name: nr for nr, name in names.items()}
    text = path.read_text()

    for line in text.splitlines():
        m = re.search(r"syscall_table_rw\[__NR_(\w+)\]\s*=", line)
        if m:
            nr = name_to_nr.get(m.group(1))
            if nr is not None:
                wired.add(nr)
            continue
        m = re.search(r"syscall_table_rw\[(\d+)\]\s*=", line)
        if m:
            wired.add(int(m.group(1)))

    wired |= parse_socket_loop_wiring(text, name_to_nr)
    return wired


def parse_semantic_decoder(path: Path, name_to_nr: dict[str, int]) -> dict[int, str]:
    """Map native Linux numbers to IR0 semantic identities."""
    decoded: dict[int, str] = {}
    text = path.read_text()
    pattern = re.compile(
        r"case\s+__NR_(\w+)\s*:\s*return\s+(IR0_SYSCALL_[A-Z0-9_]+)\s*;"
    )
    for abi_name, semantic_id in pattern.findall(text):
        nr = name_to_nr.get(abi_name)
        if nr is not None:
            decoded[nr] = semantic_id
    return decoded


def parse_semantic_handlers(path: Path) -> set[str]:
    """Return semantic identities assigned a concrete native handler."""
    text = path.read_text()
    return set(
        re.findall(
            r"syscall_semantic_table\[(IR0_SYSCALL_[A-Z0-9_]+)\]\s*=\s*"
            r"[A-Za-z_][A-Za-z0-9_]*\s*;",
            text,
        )
    )


def parse_semantic_contract(
    dispatch_path: Path, decoder_path: Path
) -> tuple[dict[int, str], set[str]]:
    """Read both sides of the native ABI-to-common-core contract."""
    names = parse_nr_define(SYSCALL_H)
    name_to_nr = {name: nr for nr, name in names.items()}
    return (
        parse_semantic_decoder(decoder_path, name_to_nr),
        parse_semantic_handlers(dispatch_path),
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tier1", action="store_true", help="show tier-1/busybox risks only")
    parser.add_argument("--musl", action="store_true", help="show musl cred/signal gaps")
    args = parser.parse_args()

    names = parse_nr_define(SYSCALL_H)
    dispatch_path = SYSCALL_DISPATCH_C if SYSCALL_DISPATCH_C.is_file() else SYSCALLS_C
    wired = parse_wired_table(dispatch_path)
    semantic_errors: list[str] = []
    if X86_DECODE_C.is_file():
        decoded, handlers = parse_semantic_contract(dispatch_path, X86_DECODE_C)
        if not decoded:
            semantic_errors.append("x86-64 decoder exposes no semantic syscall identities")
        if not handlers:
            semantic_errors.append("common dispatcher exposes no semantic syscall handlers")
        for semantic_id in sorted(set(decoded.values()) - handlers):
            semantic_errors.append(f"decoded without common handler: {semantic_id}")
        for semantic_id in sorted(handlers - set(decoded.values())):
            semantic_errors.append(f"common handler unreachable from x86-64: {semantic_id}")
        wired |= {
            nr for nr, semantic_id in decoded.items() if semantic_id in handlers
        }

    missing_tier1 = []
    for nr, label in sorted(TIER1_BUSYBOX.items()):
        sym = names.get(nr, label)
        if nr not in wired:
            missing_tier1.append((nr, sym))

    missing_musl = []
    if args.musl:
        for nr, label in sorted({**MUSL_CRED_SET, **MUSL_SIGNAL_SET}.items()):
            sym = names.get(nr, label)
            if nr not in wired:
                missing_musl.append((nr, sym))

    print("KTM_SYSCALL_MANIFEST")
    print(f"  wired_total={len(wired)}  linux_nr_defined={len(names)}")

    if semantic_errors:
        print("  semantic_contract_errors:")
        for error in semantic_errors:
            print(f"    - {error}")

    if args.tier1 or missing_tier1:
        print("  tier1_missing:")
        for nr, sym in missing_tier1:
            print(f"    - {nr:3d} __NR_{sym}  (ENOSYS → userspace spin risk for libc wrappers)")

    if args.musl or missing_musl:
        print("  musl_missing:")
        for nr, sym in missing_musl:
            print(f"    - {nr:3d} __NR_{sym}")

    if not args.tier1 and not args.musl:
        enosys_risk = [nr for nr in names if nr not in wired and nr < 512]
        print(f"  unwired_count={len(enosys_risk)} (first 20):")
        for nr in enosys_risk[:20]:
            print(f"    - {nr:3d} __NR_{names[nr]}")

    return 1 if (semantic_errors or missing_tier1 or missing_musl) else 0


if __name__ == "__main__":
    sys.exit(main())
