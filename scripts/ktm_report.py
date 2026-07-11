#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""
KTM — unified post-mortem report for a QEMU serial log.

Runs syscall manifest (tier-1 gaps) and log classification.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    parser = argparse.ArgumentParser(description="KTM unified smoke/QEMU log report")
    parser.add_argument("log", nargs="?", type=Path,
                          help="serial log (default: /tmp/runit-ash-smoke.log)")
    parser.add_argument("--no-manifest", action="store_true")
    args = parser.parse_args()

    log = args.log or Path("/tmp/runit-ash-smoke.log")

    print("KTM_REPORT")
    print(f"  log={log}")

    if not args.no_manifest:
        print("\n--- syscall manifest (tier-1) ---")
        manifest = ROOT / "scripts" / "ktm_syscall_manifest.py"
        rc = subprocess.run(
            [sys.executable, str(manifest), "--tier1"],
            cwd=ROOT,
            check=False,
        )
        if rc.returncode != 0:
            print("  (tier-1 gaps listed above — informational for T1 roadmap)")

    print("\n--- log classification ---")
    classify = ROOT / "scripts" / "ktm_log_classify.py"
    if log.is_file():
        subprocess.run([sys.executable, str(classify), str(log)], check=False)
    else:
        print(f"  log missing: {log}")
        return 1

    # KTM event summary
    if log.is_file():
        text = log.read_text(errors="replace")
        ev = [ln.strip() for ln in text.splitlines() if "[KTM][EV]" in ln]
        panic = [ln.strip() for ln in text.splitlines() if "[KTM][PANIC_CLASS]" in ln]
        ctx = [ln.strip() for ln in text.splitlines() if "[KTM][CTX]" in ln]
        print("\n--- KTM serial anchors ---")
        print(f"  events={len(ev)} panic_class={len(panic)} ctx_snapshots={len(ctx)}")
        for ln in ev[-8:]:
            print(f"    {ln}")
        for ln in panic[-4:]:
            print(f"    {ln}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
