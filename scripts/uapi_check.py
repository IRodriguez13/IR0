#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Prove that `make headers_install` exports a usable Linux UAPI per ISA.

For every ISA with a syscall decoder (arch/<isa>/sources/syscall_decode.c):

1. Run the real `make headers_install ARCH=<isa>` into a temporary DESTDIR.
2. Compile one probe TU per header listed in tests/host/uapi/required_headers.txt
   and the value probe tests/host/uapi/uapi_abi_constants.c with -nostdinc:
   only the libc headers (musl), the compiler's own include directory and the
   installed tree are searchable, in the same order ISD uses (libc first,
   UAPI through -idirafter).  The host /usr/include/linux can never satisfy a
   missing header, and the dependency list is checked for any path outside
   those roots.
3. Check asm/unistd.h against the kernel: every __NR_* it exports is a case of
   the ISA decoder, every decoder case is a named constant from asm/unistd.h
   (or an IR0 extension from the ISA's asm/unistd_ir0.h), no number is defined
   twice, and each exported value equals the Linux table found on the host.

ISAs whose cross compiler is missing are reported as SKIP; the check fails
when no ISA could be compiled at all.
"""

from __future__ import annotations

import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UAPI_TESTS = ROOT / "tests" / "host" / "uapi"
REQUIRED = UAPI_TESTS / "required_headers.txt"
CONSTANTS = UAPI_TESTS / "uapi_abi_constants.c"

# One entry per ISA: compiler triple and Linux syscall tables to compare with
# (the first one present on the host is used).
ISA_TABLE = {
    "x86-64": {
        "triple": "x86_64-linux-gnu",
        "linux_syscalls": (
            "/usr/include/x86_64-linux-musl/bits/syscall.h",
            "/usr/include/x86_64-linux-gnu/asm/unistd_64.h",
            "/usr/include/asm/unistd_64.h",
        ),
    },
    "arm64": {
        "triple": "aarch64-linux-gnu",
        "linux_syscalls": (
            "/usr/aarch64-linux-gnu/include/asm-generic/unistd.h",
            "/usr/include/asm-generic/unistd.h",
        ),
    },
}

LIBC_INCLUDE_CANDIDATES = (
    "/usr/include/x86_64-linux-musl",
    "/usr/lib/musl/include",
    "/usr/local/musl/include",
)

NR_DEFINE = re.compile(r"^\s*#\s*define\s+(__NR\w*)\s+(\w+)", re.M)
DECODER_CASE = re.compile(r"\bcase\s+([^:]+?)\s*:")


def isas() -> list[str]:
    return sorted(p.parents[1].name for p in ROOT.glob("arch/*/sources/syscall_decode.c"))


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def find_compiler(triple: str) -> str | None:
    cpu = triple.split("-")[0]
    for cc in (f"{triple}-gcc", "gcc", "cc"):
        if not shutil.which(cc):
            continue
        res = run([cc, "-dumpmachine"])
        if res.returncode == 0 and res.stdout.strip().startswith(cpu):
            return cc
    return None


def find_libc_include() -> str | None:
    override = os.environ.get("IR0_UAPI_LIBC_INCLUDE")
    if override:
        return override if Path(override, "stdint.h").is_file() else None
    for cand in LIBC_INCLUDE_CANDIDATES:
        if Path(cand, "stdint.h").is_file():
            return cand
    return None


def nr_defines(text: str) -> dict[str, list[str]]:
    out: dict[str, list[str]] = {}
    for name, value in NR_DEFINE.findall(text):
        out.setdefault(name, []).append(value)
    return out


def resolve(defs: dict[str, list[str]], name: str, depth: int = 0) -> set[int]:
    values: set[int] = set()
    if depth > 4:
        return values
    for value in defs.get(name, []):
        if value.isdigit():
            values.add(int(value))
        elif value.startswith("__NR"):
            values |= resolve(defs, value, depth + 1)
    return values


def check_syscalls(isa: str, errors: list[str]) -> str:
    """Return a one-line summary; append failures to errors."""
    uapi_asm = ROOT / "arch" / isa / "include" / "uapi" / "asm"
    header = uapi_asm / "unistd.h"
    ir0_ext = uapi_asm / "unistd_ir0.h"
    decoder = ROOT / "arch" / isa / "sources" / "syscall_decode.c"
    for required in (header, ir0_ext):
        if not required.is_file():
            # <ir0/syscall_linux.h> includes both on every ISA.
            errors.append(f"[{isa}] missing {required.relative_to(ROOT)}")
    if not header.is_file() or not ir0_ext.is_file():
        return "asm/unistd.h or asm/unistd_ir0.h missing"

    exported = nr_defines(header.read_text())
    decoder_text = decoder.read_text()
    # <ir0/syscall_linux.h> includes both headers: a name in both would
    # silently replace the Linux number on that ISA.
    ext = nr_defines(ir0_ext.read_text())
    for name in sorted(set(exported) & set(ext)):
        errors.append(f"[{isa}] {name} defined in both asm/unistd.h and asm/unistd_ir0.h")
    # IR0-specific numbers only count for a decoder that includes them.
    if "asm/unistd_ir0.h" not in decoder_text:
        ext = {}
    for name, values in exported.items():
        if len(values) != 1 or not values[0].isdigit():
            errors.append(f"[{isa}] asm/unistd.h: {name} must be one decimal number")

    cases = DECODER_CASE.findall(decoder_text)
    named = set()
    for label in cases:
        if not re.fullmatch(r"__NR_\w+", label):
            errors.append(f"[{isa}] {decoder.relative_to(ROOT)}: case {label}: use an asm/unistd.h name")
            continue
        named.add(label)
        if label not in exported and label not in ext:
            errors.append(f"[{isa}] decoder case {label} is not exported by asm/unistd.h")
    for name in sorted(set(exported) - named):
        errors.append(f"[{isa}] asm/unistd.h exports {name} but the decoder does not dispatch it")

    ref = next((Path(p) for p in ISA_TABLE[isa]["linux_syscalls"] if Path(p).is_file()), None)
    if ref is None:
        return f"{len(exported)} syscalls, Linux table SKIP (none on host)"
    linux = nr_defines(ref.read_text())
    newer = 0
    for name, values in sorted(exported.items()):
        want = resolve(linux, name)
        if not want:
            # Host headers older than the syscall: nothing to compare with.
            newer += 1
        elif values[0].isdigit() and int(values[0]) not in want:
            errors.append(f"[{isa}] {name}={values[0]} but Linux ({ref}) has {sorted(want)}")
    return (f"{len(exported)} syscalls == decoder, values == {ref}"
            f" ({newer} newer than that table, not compared)")


def compile_probe(cc: str, src: Path, inc: dict[str, str], tmp: Path) -> str | None:
    """Compile src; return an error string or None."""
    obj = tmp / (src.stem + ".o")
    dep = tmp / (src.stem + ".d")
    cmd = [cc, "-std=gnu11", "-nostdinc",
           "-isystem", inc["libc"], "-isystem", inc["cc"],
           "-idirafter", inc["uapi"],
           "-Werror", "-c", str(src), "-o", str(obj), "-MD", "-MF", str(dep)]
    res = run(cmd)
    if res.returncode != 0:
        lines = [l for l in res.stderr.splitlines() if "error" in l][:3]
        return "; ".join(lines) or res.stderr.strip()[:300]
    roots = [os.path.realpath(p) for p in (inc["libc"], inc["cc"], inc["uapi"], str(tmp), str(UAPI_TESTS))]
    for path in dep.read_text().replace("\\\n", " ").split()[1:]:
        real = os.path.realpath(path)
        if not any(real == r or real.startswith(r + os.sep) for r in roots):
            return f"resolved outside the installed UAPI: {path}"
    return None


def check_isa(isa: str, libc: str | None, tmp: Path, errors: list[str]) -> str:
    if isa not in ISA_TABLE:
        errors.append(f"[{isa}] no ISA_TABLE entry in scripts/uapi_check.py")
        return "FAIL"
    summary = check_syscalls(isa, errors)
    print(f"  {isa}: {summary}")

    dest = tmp / isa
    res = run(["make", "-s", "-C", str(ROOT), "headers_install", f"ARCH={isa}", f"DESTDIR={dest}"])
    if res.returncode != 0:
        errors.append(f"[{isa}] make headers_install failed: {res.stderr.strip()[:300]}")
        return "FAIL"

    cc = find_compiler(ISA_TABLE[isa]["triple"])
    if cc is None or libc is None:
        why = "no compiler for " + ISA_TABLE[isa]["triple"] if cc is None else "no musl headers"
        print(f"  {isa}: header probes SKIP ({why})")
        return "SKIP"
    inc = {"libc": libc, "cc": run([cc, "-print-file-name=include"]).stdout.strip(),
           "uapi": str(dest / "usr" / "include")}

    work = tmp / f"{isa}-probe"
    work.mkdir()
    headers = [l.strip() for l in REQUIRED.read_text().splitlines()
               if l.strip() and not l.startswith("#")]
    failed = 0
    for i, header in enumerate(headers):
        src = work / f"probe_{i:02d}.c"
        src.write_text(f"#include <{header}>\n")
        err = compile_probe(cc, src, inc, work)
        if err:
            failed += 1
            errors.append(f"[{isa}] <{header}>: {err}")
    err = compile_probe(cc, CONSTANTS, inc, work)
    if err:
        errors.append(f"[{isa}] {CONSTANTS.name}: {err}")
    print(f"  {isa}: {len(headers) - failed}/{len(headers)} headers compile with {cc}, "
          f"ABI values {'FAIL' if err else 'OK'}")
    return "RAN"


def main() -> int:
    errors: list[str] = []
    libc = find_libc_include()
    ran = 0
    with tempfile.TemporaryDirectory(prefix="ir0-uapi-check-") as tmp:
        for isa in isas():
            if check_isa(isa, libc, Path(tmp), errors) == "RAN":
                ran += 1
    for err in errors:
        print(f"✗ {err}", file=sys.stderr)
    if ran == 0 and not errors:
        print("✗ uapi-check: no ISA could be compiled (need musl headers and a target gcc)",
              file=sys.stderr)
        return 1
    if errors:
        print(f"✗ uapi-check FAILED ({len(errors)} problems)", file=sys.stderr)
        return 1
    print("✓ uapi-check OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
