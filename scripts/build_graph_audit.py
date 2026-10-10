#!/usr/bin/env python3
"""Validate the effective object inventory emitted by the IR0 Makefile."""

# SPDX-License-Identifier: GPL-3.0-only

from __future__ import annotations

import argparse
import pathlib
import subprocess
import sys
from collections import Counter


SOURCE_SUFFIXES = (".c", ".cpp", ".S", ".s", ".asm")

# These objects intentionally originate from an explicit Make rule, not a
# source file with the same stem. Keep this list small and review every entry.
GENERATED_OBJECTS = {
    "build/arm64-boot/rr_sched.o",
    "build/arm64-boot/switch_arm64.o",
    "arch/x86-64/vdso/vdso_blob_embed.o",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=pathlib.Path)
    parser.add_argument("--make", required=True)
    return parser.parse_args()


def object_has_source(root: pathlib.Path, obj: str) -> bool:
    if obj in GENERATED_OBJECTS:
        return True
    if not obj.endswith(".o"):
        return False
    stem = root / obj[:-2]
    return any(stem.with_suffix(suffix).is_file() for suffix in SOURCE_SUFFIXES)


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    result = subprocess.run(
        [args.make, "-s", "--no-print-directory", "print-build-objects"],
        cwd=root,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        check=False,
    )
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        return result.returncode

    objects = result.stdout.split()
    duplicates = sorted(name for name, count in Counter(objects).items() if count > 1)
    unmapped = sorted(name for name in objects if not object_has_source(root, name))

    if duplicates:
        print("build-graph-audit: duplicate effective objects:", file=sys.stderr)
        print("\n".join(f"  {name}" for name in duplicates), file=sys.stderr)
    if unmapped:
        print("build-graph-audit: objects without a direct source file:", file=sys.stderr)
        print("\n".join(f"  {name}" for name in unmapped), file=sys.stderr)

    if duplicates or unmapped:
        return 1

    print(f"[build-graph-audit] OK ({len(objects)} effective objects)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
