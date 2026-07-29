#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
Userspace stability harness (wrapper around smoke_desktop_cmd_matrix --stability).

Exercises product binaries that previously exposed IRQ/sched/TTY instability:
  top (batch + interactive quit), man quit, find, dmesg file/pipe, pipe chains,
  /proc/stat, tcc compile+run via 9p. Optional --with-doom when WAD is present.

Default disk: ISD development image (has top/man/tcc).
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
from pathlib import Path
from typing import Optional

ROOT = Path(__file__).resolve().parents[1]
ISD = ROOT.parent / "ISD"
MATRIX = ROOT / "scripts" / "smoke_desktop_cmd_matrix.py"

DEFAULT_DISK = ISD / "out/x86_64/images/development/disk.img"
DEFAULT_WADS = [
    Path("/home/ivanr013/Escritorio/universal-doom/DOOM1.WAD"),
    Path.home() / "Escritorio/universal-doom/DOOM1.WAD",
    ROOT / "DOOM1.WAD",
    ROOT / "doom1.wad",
]


def find_wad(explicit: Optional[Path]) -> Optional[Path]:
    if explicit is not None and explicit.is_file():
        return explicit
    for p in DEFAULT_WADS:
        if p.is_file():
            return p
    return None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--iso", type=Path, default=ROOT / "kernel-x64-userspace.iso")
    ap.add_argument("--disk", type=Path, default=DEFAULT_DISK)
    ap.add_argument("--log", type=Path, default=Path("/tmp/ir0-userspace-stability.log"))
    ap.add_argument("--rounds", type=int, default=1)
    ap.add_argument("--port", type=int, default=46101)
    ap.add_argument("--key-delay", type=float, default=0.40)
    ap.add_argument("--batch-size", type=int, default=3)
    ap.add_argument("--su", action="store_true")
    ap.add_argument("--with-doom", action="store_true", help="Require doom WAD")
    ap.add_argument(
        "--auto-doom",
        action="store_true",
        help="Enable doom if a known WAD path exists (default for make target)",
    )
    ap.add_argument("--no-tcc", action="store_true")
    ap.add_argument("--doom-bin", type=Path, default=ROOT / "setup/pid1/fase55e_doom_interactive")
    ap.add_argument("--wad", type=Path, default=None)
    args = ap.parse_args()

    if not MATRIX.is_file():
        print(f"✗ missing {MATRIX}", file=sys.stderr)
        return 2
    if not args.iso.is_file() or not args.disk.is_file():
        print(f"✗ need iso+disk: {args.iso} {args.disk}", file=sys.stderr)
        return 2

    wad = find_wad(args.wad)
    want_doom = args.with_doom or (args.auto_doom and wad is not None)
    if args.with_doom and wad is None:
        print("✗ --with-doom but no DOOM1.WAD found", file=sys.stderr)
        return 2
    if args.auto_doom and wad is None:
        print("note: doom skipped (no DOOM1.WAD on host)", flush=True)

    cmd = [
        sys.executable,
        str(MATRIX),
        "--stability",
        "--iso",
        str(args.iso),
        "--disk",
        str(args.disk),
        "--log",
        str(args.log),
        "--rounds",
        str(args.rounds),
        "--port",
        str(args.port),
        "--key-delay",
        str(args.key_delay),
        "--batch-size",
        str(args.batch_size),
        "--skip-poweroff",
    ]
    if args.su:
        cmd.append("--su")
    if args.no_tcc:
        cmd.append("--no-tcc")
    if want_doom and wad is not None:
        cmd += ["--with-doom", "--doom-bin", str(args.doom_bin), "--wad", str(wad)]

    print("USERSPACE_STABILITY:", " ".join(cmd), flush=True)
    env = os.environ.copy()
    return subprocess.call(cmd, cwd=str(ROOT), env=env)


if __name__ == "__main__":
    sys.exit(main())
