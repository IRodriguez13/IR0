#!/usr/bin/env python3
"""
Stable digest of externally visible kernel ELF symbols (for deterministic rebuild checks).

Requires: nm (from GNU binutils). Pass path to kernel-x64.bin (or other linked ELF).

Example:
  ./scripts/kernel_export_digest.py kernel-x64.bin
"""

from __future__ import annotations

import hashlib
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: kernel_export_digest.py <path-to-kernel-elf>", file=sys.stderr)
        return 2
    path = Path(sys.argv[1])
    if not path.is_file():
        print(f"error: not a file: {path}", file=sys.stderr)
        return 1
    try:
        out = subprocess.check_output(
            ["nm", "-g", "--defined-only", str(path)],
            stderr=subprocess.DEVNULL,
            text=True,
        )
    except FileNotFoundError:
        print("error: `nm` not found (install binutils)", file=sys.stderr)
        return 1
    except subprocess.CalledProcessError as e:
        print(f"error: nm failed: {e}", file=sys.stderr)
        return 1

    lines = [ln.strip() for ln in out.splitlines() if ln.strip()]
    lines.sort()
    blob = "\n".join(lines).encode("utf-8")
    digest = hashlib.sha256(blob).hexdigest()
    print(f"sha256:{digest}")
    print(f"symbols:{len(lines)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
